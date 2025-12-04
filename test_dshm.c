#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// Include our distributed SHM client
#include "distributed_shm_client.h"

int main() {
    printf("Тестирование распределенной разделяемой памяти\n");

    // Initialize the distributed SHM client
    if (distributed_shm_init("localhost", 8080) != 0) {
        perror("distributed_shm_init failed");
        return 1;
    }

    printf("Клиент инициализирован\n");

    // Generate a key for the shared memory segment
    key_t key = ftok(".", 'R');  // Using a key based on current directory
    if (key == -1) {
        perror("ftok");
        key = 0x12345;  // Fallback key
    }

    printf("Используем ключ: 0x%x\n", key);

    // Create a shared memory segment of 4096 bytes
    int shmid = shmget(key, 4096, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        distributed_shm_cleanup();
        return 1;
    }

    printf("Создан сегмент разделяемой памяти с ID: %d\n", shmid);

    // Attach to the shared memory segment
    char *shm_ptr = (char*)shmat(shmid, NULL, 0);
    if (shm_ptr == (char*)-1) {
        perror("shmat");
        distributed_shm_cleanup();
        return 1;
    }

    printf("Присоединились к сегменту разделяемой памяти\n");

    // Write some data to the shared memory
    const char *test_data = "Hello from distributed shared memory!";
    size_t data_len = strlen(test_data) + 1;  // Include null terminator
    strncpy(shm_ptr, test_data, data_len);
    printf("Записали данные: %s\n", test_data);

    // Read the data back to verify
    printf("Прочитанные данные: %s\n", shm_ptr);

    // Test shmctl - get status
    struct shmid_ds shmbuf;
    if (shmctl(shmid, IPC_STAT, &shmbuf) == -1) {
        perror("shmctl IPC_STAT");
    } else {
        printf("Размер сегмента: %ld байт\n", (long)shmbuf.shm_segsz);
        printf("Количество присоединенных процессов: %ld\n", (long)shmbuf.shm_nattch);
    }

    // Detach from the shared memory segment
    if (shmdt(shm_ptr) == -1) {
        perror("shmdt");
        distributed_shm_cleanup();
        return 1;
    }

    printf("Отсоединились от сегмента разделяемой памяти\n");

    // Remove the shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID");
        distributed_shm_cleanup();
        return 1;
    }

    printf("Удалили сегмент разделяемой памяти\n");

    // Cleanup the client library
    distributed_shm_cleanup();
    printf("Тестирование завершено успешно\n");

    return 0;
}
