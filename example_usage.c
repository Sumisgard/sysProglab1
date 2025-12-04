/*
 * Пример использования распределенной разделяемой памяти
 * 
 * Этот пример показывает, как использовать библиотеку распределенной 
 * разделяемой памяти с интерфейсом, совместимым с POSIX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>

// Подключаем нашу клиентскую библиотеку
#include "distributed_shm_client.h"

int main() {
    printf("=== Пример использования распределенной разделяемой памяти ===\n\n");
    
    // 1. Инициализация клиентской библиотеки
    printf("1. Инициализация клиентской библиотеки...\n");
    if (distributed_shm_init("localhost", 8080) != 0) {
        perror("distributed_shm_init");
        return 1;
    }
    printf("   Клиент инициализирован успешно\n\n");

    // 2. Создание сегмента разделяемой памяти
    printf("2. Создание сегмента разделяемой памяти...\n");
    key_t key = ftok(".", 'X');  // Генерируем уникальный ключ
    if (key == -1) {
        key = 0x12345678;  // Резервный ключ
        printf("   Используем резервный ключ: 0x%x\n", key);
    } else {
        printf("   Сгенерированный ключ: 0x%x\n", key);
    }

    int shmid = shmget(key, 4096, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        distributed_shm_cleanup();
        return 1;
    }
    printf("   Сегмент создан с ID: %d\n\n", shmid);

    // 3. Присоединение к сегменту
    printf("3. Присоединение к сегменту...\n");
    char *shm_ptr = (char*)shmat(shmid, NULL, 0);
    if (shm_ptr == (char*)-1) {
        perror("shmat");
        distributed_shm_cleanup();
        return 1;
    }
    printf("   Успешно присоединились к сегменту\n\n");

    // 4. Работа с данными в разделяемой памяти
    printf("4. Работа с данными в разделяемой памяти...\n");
    
    // Запись данных
    const char *message = "Hello from distributed shared memory!";
    strcpy(shm_ptr, message);
    printf("   Записали данные: \"%s\"\n", message);
    
    // Чтение данных
    printf("   Прочитали данные: \"%s\"\n", shm_ptr);
    
    // Модификация данных
    strcat(shm_ptr, " [MODIFIED]");
    printf("   Изменили данные: \"%s\"\n\n", shm_ptr);

    // 5. Получение информации о сегменте
    printf("5. Получение информации о сегменте...\n");
    struct shmid_ds buf;
    if (shmctl(shmid, IPC_STAT, &buf) == -1) {
        perror("shmctl IPC_STAT");
    } else {
        printf("   Размер сегмента: %zu байт\n", buf.shm_segsz);
        printf("   Количество присоединенных процессов: %ld\n", buf.shm_nattch);
        printf("   Режим доступа: 0%o\n", buf.shm_perm.mode & 0777);
    }
    printf("\n");

    // 6. Отсоединение от сегмента
    printf("6. Отсоединение от сегмента...\n");
    if (shmdt(shm_ptr) == -1) {
        perror("shmdt");
        distributed_shm_cleanup();
        return 1;
    }
    printf("   Успешно отсоединились от сегмента\n\n");

    // 7. Удаление сегмента
    printf("7. Удаление сегмента...\n");
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID");
        distributed_shm_cleanup();
        return 1;
    }
    printf("   Сегмент успешно удален\n\n");

    // 8. Очистка клиентской библиотеки
    printf("8. Очистка ресурсов...\n");
    distributed_shm_cleanup();
    printf("   Ресурсы очищены успешно\n\n");

    printf("=== Пример завершен успешно ===\n");
    printf("Библиотека распределенной разделяемой памяти полностью совместима с POSIX!\n");

    return 0;
}
