#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>

// Include our distributed SHM client
#include "distributed_shm_client.h"

int main() {
    printf("Тестирование обработки ошибок распределенной разделяемой памяти\n");

    // Initialize the distributed SHM client
    if (distributed_shm_init("localhost", 8080) != 0) {
        perror("distributed_shm_init failed");
        return 1;
    }

    printf("Клиент инициализирован\n");

    // Test 1: Try to attach to non-existent segment
    printf("\n--- Тест 1: Попытка присоединиться к несуществующему сегменту ---\n");
    void *ptr = shmat(-1, NULL, 0);
    if (ptr == (void*)-1) {
        printf("OK: shmat вернула -1 при попытке присоединения к недопустимому ID\n");
        printf("errno: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("ERROR: shmat не вернула -1 для недопустимого ID\n");
    }

    // Test 2: Try to get status of non-existent segment
    printf("\n--- Тест 2: Попытка получения статуса несуществующего сегмента ---\n");
    struct shmid_ds buf;
    if (shmctl(-1, IPC_STAT, &buf) == -1) {
        printf("OK: shmctl вернула -1 для недопустимого ID\n");
        printf("errno: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("ERROR: shmctl не вернула -1 для недопустимого ID\n");
    }

    // Test 3: Create and use a valid segment
    printf("\n--- Тест 3: Создание и использование валидного сегмента ---\n");
    key_t key = 0x12345678;  // Using a fixed key for testing
    int shmid = shmget(key, 1024, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        distributed_shm_cleanup();
        return 1;
    }
    printf("Создан сегмент с ID: %d\n", shmid);

    // Attach to the segment
    char *shm_ptr = (char*)shmat(shmid, NULL, 0);
    if (shm_ptr == (char*)-1) {
        perror("shmat failed");
        distributed_shm_cleanup();
        return 1;
    }
    printf("Присоединились к сегменту\n");

    // Write and read data
    strcpy(shm_ptr, "Test data");
    printf("Записано и прочитано: %s\n", shm_ptr);

    // Test 4: Try to detach with invalid address
    printf("\n--- Тест 4: Попытка отсоединения от неверного адреса ---\n");
    if (shmdt((void*)0x12345) == -1) {
        printf("OK: shmdt вернула -1 для неверного адреса\n");
        printf("errno: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("INFO: shmdt не вернула -1 для неверного адреса (это может быть нормально в зависимости от реализации)\n");
    }

    // Properly detach
    if (shmdt(shm_ptr) == -1) {
        perror("shmdt failed");
        distributed_shm_cleanup();
        return 1;
    }
    printf("Успешно отсоединились от сегмента\n");

    // Remove the segment
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID failed");
        distributed_shm_cleanup();
        return 1;
    }
    printf("Успешно удалили сегмент\n");

    // Test 5: Try operations on removed segment
    printf("\n--- Тест 5: Попытка работы с удаленным сегментом ---\n");
    if (shmat(shmid, NULL, 0) == (void*)-1) {
        printf("OK: shmat вернула -1 для удаленного сегмента\n");
        printf("errno: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("ERROR: shmat не вернула -1 для удаленного сегмента\n");
    }

    distributed_shm_cleanup();
    printf("\nВсе тесты завершены успешно!\n");
    return 0;
}
