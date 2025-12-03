#ifndef DISTRIBUTED_SHM_H
#define DISTRIBUTED_SHM_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdint.h>
#include <netinet/in.h>

// Определения команд протокола
typedef enum {
    CMD_CREATE_SEGMENT = 1,
    CMD_ATTACH_SEGMENT,
    CMD_DETACH_SEGMENT,
    CMD_REMOVE_SEGMENT,
    CMD_READ_DATA,
    CMD_WRITE_DATA,
    CMD_GET_STATUS,
    CMD_SHMCTL
} shm_command_t;

// Структура заголовка сообщения
typedef struct {
    uint32_t command;      // Команда
    uint32_t size;         // Размер данных в сообщении
    int32_t shmid;         // ID сегмента разделяемой памяти
    int32_t flags;         // Флаги (для shmget)
    uint32_t offset;       // Смещение для чтения/записи
} shm_header_t;

// Структура для ответа
typedef struct {
    int32_t result;        // Результат выполнения команды
    int32_t error_code;    // Код ошибки (если есть)
    uint32_t data_size;    // Размер возвращаемых данных
} shm_response_t;

// Структура для хранения информации о сегменте
typedef struct {
    int shmid;              // ID сегмента
    void *addr;             // Адрес в памяти сервера
    size_t size;            // Размер сегмента
    int shmflg;             // Флаги
    int ref_count;          // Счетчик ссылок
    int attached_clients;   // Количество подключенных клиентов
} shm_segment_t;

// Определения размеров
#define MAX_SEGMENTS 1024
#define MAX_CLIENTS 100
#define MAX_BUFFER_SIZE 65536

// Определения ошибок
#define SHM_SUCCESS 0
#define SHM_ERROR -1
#define SHM_EINVAL -2
#define SHM_ENOMEM -3
#define SHM_EACCES -4
#define SHM_ENOENT -5

#endif // DISTRIBUTED_SHM_H
