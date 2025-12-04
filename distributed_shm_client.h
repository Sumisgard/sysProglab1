#ifndef DISTRIBUTED_SHM_CLIENT_H
#define DISTRIBUTED_SHM_CLIENT_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdint.h>
#include <stddef.h>

// Configuration for the client
#define DEFAULT_SERVER_HOST "localhost"
#define DEFAULT_SERVER_PORT 8080

// Client-side structure to maintain local mapping
typedef struct {
    int shmid;              // Shared memory ID from server
    void *local_addr;       // Local address where data is cached/stored
    size_t size;            // Size of the shared memory segment
    int attached;           // Flag indicating if currently attached
    int shmflg;             // Flags used when creating/attaching
} client_shm_segment_t;

// Maximum number of segments a client can handle
#define MAX_CLIENT_SEGMENTS 1024

// Client library functions - POSIX compatible interface
extern int distributed_shmget(key_t key, size_t size, int shmflg);
extern void *distributed_shmat(int shmid, const void *shmaddr, int shmflg);
extern int distributed_shmdt(const void *shmaddr);
extern int distributed_shmctl(int shmid, int cmd, struct shmid_ds *buf);

// Client initialization and cleanup functions
extern int distributed_shm_init(const char *server_host, int server_port);
extern void distributed_shm_cleanup(void);

// Internal functions (not part of public API)
extern int connect_to_server(void);
extern int send_request_to_server(uint32_t command, int shmid, int flags, uint32_t offset, 
                                  void *data, size_t data_size, void **response_data, size_t *response_size);

#endif // DISTRIBUTED_SHM_CLIENT_H
