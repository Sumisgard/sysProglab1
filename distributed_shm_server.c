#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "distributed_shm.h"

// Определение MAP_ANONYMOUS для систем, где оно может отсутствовать
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

// Глобальные переменные
static shm_segment_t segments[MAX_SEGMENTS];
static int segment_count = 0;
static pthread_mutex_t segments_mutex = PTHREAD_MUTEX_INITIALIZER;
static int server_socket = -1;
static int running = 1;

// Функция для поиска сегмента по ID
static shm_segment_t* find_segment(int shmid) {
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (segments[i].shmid == shmid && segments[i].addr != NULL) {
            return &segments[i];
        }
    }
    return NULL;
}

// Функция для создания нового сегмента
static shm_segment_t* create_segment(int shmid, size_t size, int shmflg) {
    // Проверяем, существует ли уже сегмент с таким ID
    if (find_segment(shmid) != NULL) {
        return NULL; // Сегмент уже существует
    }

    // Ищем свободный слот
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (segments[i].addr == NULL) {
            // Создаем сегмент в памяти
            void *addr = mmap(NULL, size, 
                             (shmflg & SHM_RDONLY) ? PROT_READ : PROT_READ | PROT_WRITE,
                             MAP_ANONYMOUS | MAP_SHARED, -1, 0);
            
            if (addr == MAP_FAILED) {
                return NULL; // Ошибка выделения памяти
            }

            segments[i].shmid = shmid;
            segments[i].addr = addr;
            segments[i].size = size;
            segments[i].shmflg = shmflg;
            segments[i].ref_count = 0;
            segments[i].attached_clients = 0;
            
            segment_count++;
            return &segments[i];
        }
    }
    
    return NULL; // Нет свободных слотов
}

// Функция для удаления сегмента
static int remove_segment(int shmid) {
    shm_segment_t *segment = find_segment(shmid);
    if (segment == NULL) {
        return SHM_ENOENT;
    }

    // Проверяем, есть ли присоединенные клиенты
    if (segment->attached_clients > 0) {
        // Устанавливаем флаг на удаление после отсоединения всех клиентов
        segment->shmflg |= IPC_RMID; // Помечаем для удаления
        return SHM_SUCCESS;
    }

    // Освобождаем память
    if (segment->addr != NULL) {
        munmap(segment->addr, segment->size);
    }

    // Очищаем запись
    memset(segment, 0, sizeof(shm_segment_t));
    segment_count--;
    
    return SHM_SUCCESS;
}

// Обработка команды создания сегмента
static int handle_create_segment(shm_header_t *header, void *data) {
    size_t size = *(size_t*)data;
    int shmid = header->shmid;
    int flags = header->flags;
    
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = create_segment(shmid, size, flags);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return (errno == ENOMEM) ? SHM_ENOMEM : SHM_EINVAL;
    }
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка команды чтения данных
static int handle_read_data(shm_header_t *header, void *buffer, size_t buffer_size __attribute__((unused))) {
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = find_segment(header->shmid);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_ENOENT;
    }
    
    // Проверяем, не выходит ли за границы
    if (header->offset + header->size > segment->size) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_EINVAL;
    }
    
    // Копируем данные из сегмента в буфер
    memcpy(buffer, (char*)segment->addr + header->offset, header->size);
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка команды записи данных
static int handle_write_data(shm_header_t *header, void *data) {
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = find_segment(header->shmid);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_ENOENT;
    }
    
    // Проверяем, что сегмент не только для чтения
    if (segment->shmflg & SHM_RDONLY) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_EACCES;
    }
    
    // Проверяем, не выходит ли за границы
    if (header->offset + header->size > segment->size) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_EINVAL;
    }
    
    // Копируем данные из буфера в сегмент
    memcpy((char*)segment->addr + header->offset, data, header->size);
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка команды присоединения к сегменту
static int handle_attach_segment(shm_header_t *header) {
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = find_segment(header->shmid);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_ENOENT;
    }
    
    segment->attached_clients++;
    segment->ref_count++;
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка команды отсоединения от сегмента
static int handle_detach_segment(shm_header_t *header) {
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = find_segment(header->shmid);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_ENOENT;
    }
    
    if (segment->attached_clients > 0) {
        segment->attached_clients--;
        segment->ref_count--;
    }
    
    // Если сегмент помечен для удаления и больше нет клиентов, удаляем его
    if ((segment->shmflg & IPC_RMID) && segment->attached_clients == 0) {
        if (segment->addr != NULL) {
            munmap(segment->addr, segment->size);
        }
        memset(segment, 0, sizeof(shm_segment_t));
        segment_count--;
    }
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка команды удаления сегмента
static int handle_remove_segment(shm_header_t *header) {
    return remove_segment(header->shmid);
}

// Обработка команды shmctl
static int handle_shmctl(shm_header_t *header, void *data) {
    pthread_mutex_lock(&segments_mutex);
    
    shm_segment_t *segment = find_segment(header->shmid);
    if (segment == NULL) {
        pthread_mutex_unlock(&segments_mutex);
        return SHM_ENOENT;
    }
    
    // Обработка различных команд shmctl
    switch (header->flags) {
        case IPC_RMID:
            // Помечаем сегмент для удаления
            segment->shmflg |= IPC_RMID;
            if (segment->attached_clients == 0) {
                // Если нет присоединенных клиентов, удаляем сразу
                if (segment->addr != NULL) {
                    munmap(segment->addr, segment->size);
                }
                memset(segment, 0, sizeof(shm_segment_t));
                segment_count--;
            }
            break;
            
        case IPC_STAT:
            // Возвращаем статус сегмента
            // Для простоты возвращаем базовую информацию
            if (data != NULL) {
                struct shmid_ds *buf = (struct shmid_ds*)data;
                buf->shm_segsz = segment->size;
                buf->shm_nattch = segment->attached_clients;
                buf->shm_perm.mode = segment->shmflg;
            }
            break;
            
        case IPC_SET:
            // Устанавливаем параметры сегмента
            if (data != NULL) {
                struct shmid_ds *buf = (struct shmid_ds*)data;
                segment->shmflg = buf->shm_perm.mode;
            }
            break;
            
        default:
            pthread_mutex_unlock(&segments_mutex);
            return SHM_EINVAL;
    }
    
    pthread_mutex_unlock(&segments_mutex);
    return SHM_SUCCESS;
}

// Обработка одного запроса клиента
static void process_request(int client_socket) {
    shm_header_t header;
    ssize_t bytes_received;
    
    // Читаем заголовок запроса
    bytes_received = recv(client_socket, &header, sizeof(header), 0);
    if (bytes_received <= 0) {
        return; // Ошибка или соединение закрыто
    }
    
    // Преобразуем поля заголовка из сетевого порядка байт
    header.command = ntohl(header.command);
    header.size = ntohl(header.size);
    header.shmid = ntohl(header.shmid);
    header.flags = ntohl(header.flags);
    header.offset = ntohl(header.offset);
    
    void *data = NULL;
    shm_response_t response = {0};
    int result = SHM_SUCCESS;
    
    // Выделяем память для данных, если они есть
    if (header.size > 0) {
        data = malloc(header.size);
        if (data == NULL) {
            response.result = SHM_ENOMEM;
            goto send_response;
        }
        
        // Читаем данные
        bytes_received = recv(client_socket, data, header.size, 0);
        if (bytes_received <= 0) {
            free(data);
            return; // Ошибка или соединение закрыто
        }
    }
    
    // Обрабатываем команду
    switch (header.command) {
        case CMD_CREATE_SEGMENT:
            result = handle_create_segment(&header, data);
            break;
            
        case CMD_ATTACH_SEGMENT:
            result = handle_attach_segment(&header);
            break;
            
        case CMD_DETACH_SEGMENT:
            result = handle_detach_segment(&header);
            break;
            
        case CMD_REMOVE_SEGMENT:
            result = handle_remove_segment(&header);
            break;
            
        case CMD_READ_DATA:
            {
                // Для чтения данных нужно выделить буфер
                void *read_buffer = malloc(header.size);
                if (read_buffer != NULL) {
                    result = handle_read_data(&header, read_buffer, header.size);
                    if (result == SHM_SUCCESS) {
                        // Отправляем данные клиенту
                        if (send(client_socket, read_buffer, header.size, 0) != (ssize_t)header.size) {
                            result = SHM_ERROR; // Ошибка отправки
                        }
                    }
                    free(read_buffer);
                } else {
                    result = SHM_ENOMEM;
                }
            }
            break;
            
        case CMD_WRITE_DATA:
            result = handle_write_data(&header, data);
            break;
            
        case CMD_SHMCTL:
            result = handle_shmctl(&header, data);
            break;
            
        default:
            result = SHM_EINVAL;
            break;
    }
    
    response.result = result;
    response.error_code = (result < 0) ? -result : 0;
    response.data_size = (header.command == CMD_READ_DATA) ? header.size : 0;
    
send_response:
    // Преобразуем поля ответа в сетевой порядок байт
    shm_response_t net_response = {
        .result = htonl(response.result),
        .error_code = htonl(response.error_code),
        .data_size = htonl(response.data_size)
    };
    
    // Отправляем ответ клиенту
    if (send(client_socket, &net_response, sizeof(net_response), 0) != sizeof(net_response)) {
        // Ошибка отправки ответа
    }
    
    // Освобождаем память
    if (data != NULL) {
        free(data);
    }
}

// Функция обработки клиента (для потока)
static void* handle_client(void *arg) {
    int client_socket = *(int*)arg;
    free(arg); // Освобождаем выделенную память
    
    // Устанавливаем таймаут для сокета
    struct timeval timeout;
    timeout.tv_sec = 30; // 30 секунд
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Обрабатываем запросы от клиента
    while (running) {
        process_request(client_socket);
    }
    
    close(client_socket);
    return NULL;
}

// Обработчик сигнала для корректного завершения
static void signal_handler(int sig __attribute__((unused))) {
    running = 0;
    if (server_socket != -1) {
        close(server_socket);
    }
    printf("\nСервер остановлен.\n");
}

// Основная функция сервера
int main(int argc, char *argv[]) {
    int port = 8080; // Порт по умолчанию
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Обработка аргументов командной строки
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Неверный номер порта. Используйте значение от 1 до 65535.\n");
            return 1;
        }
    }
    
    // Регистрируем обработчик сигнала для корректного завершения
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Создаем TCP сокет
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Не удалось создать сокет");
        return 1;
    }
    
    // Устанавливаем опцию для повторного использования адреса
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Не удалось установить опцию сокета SO_REUSEADDR");
        close(server_socket);
        return 1;
    }
    
    // Настраиваем адрес сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Привязываем сокет к адресу
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Не удалось привязать сокет к адресу");
        close(server_socket);
        return 1;
    }
    
    // Начинаем прослушивание
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Ошибка при начале прослушивания");
        close(server_socket);
        return 1;
    }
    
    printf("Сервер распределенной памяти запущен на порту %d\n", port);
    printf("Ожидание подключений...\n");
    
    // Инициализация массива сегментов
    memset(segments, 0, sizeof(segments));
    
    // Цикл обработки подключений
    while (running) {
        // Принимаем новое подключение
        int *client_socket = malloc(sizeof(int));
        if (client_socket == NULL) {
            fprintf(stderr, "Ошибка выделения памяти для сокета клиента\n");
            continue;
        }
        
        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (*client_socket == -1) {
            if (running) { // Если сервер еще работает, выводим ошибку
                perror("Ошибка при принятии подключения");
            }
            free(client_socket);
            continue;
        }
        
        printf("Подключен клиент: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Создаем поток для обработки клиента
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_socket) != 0) {
            perror("Ошибка создания потока для клиента");
            close(*client_socket);
            free(client_socket);
            continue;
        }
        
        // Отсоединяем поток, чтобы он автоматически завершался
        pthread_detach(client_thread);
    }
    
    // Закрываем серверный сокет
    close(server_socket);
    
    // Освобождаем все сегменты памяти
    pthread_mutex_lock(&segments_mutex);
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (segments[i].addr != NULL) {
            munmap(segments[i].addr, segments[i].size);
        }
    }
    pthread_mutex_unlock(&segments_mutex);
    
    printf("Сервер завершен.\n");
    return 0;
}
