#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

#include "distributed_shm.h"
#include "distributed_shm_client.h"

// Global variables for client state
static client_shm_segment_t client_segments[MAX_CLIENT_SEGMENTS];
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static int server_socket_fd = -1;
static char server_host[256] = DEFAULT_SERVER_HOST;
static int server_port = DEFAULT_SERVER_PORT;
static int client_initialized = 0;

// Function to connect to the server
int connect_to_server(void) {
    if (server_socket_fd != -1) {
        close(server_socket_fd);
    }

    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    // Try to convert the hostname to an IP address
    if (inet_pton(AF_INET, server_host, &server_addr.sin_addr) <= 0) {
        // If inet_pton fails, try to resolve hostname using gethostbyname
        struct hostent *host_entry;
        host_entry = gethostbyname(server_host);
        if (host_entry == NULL) {
            fprintf(stderr, "Cannot resolve hostname: %s\n", server_host);
            close(server_socket_fd);
            server_socket_fd = -1;
            return -1;
        }
        
        // Copy the resolved IP address
        memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    }

    if (connect(server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(server_socket_fd);
        server_socket_fd = -1;
        return -1;
    }

    return server_socket_fd;
}

// Function to send a request to the server and receive a response
int send_request_to_server(uint32_t command, int shmid, int flags, uint32_t offset,
                          void *data, size_t data_size, void **response_data, size_t *response_size) {
    if (!client_initialized) {
        errno = EINVAL;
        return -1;
    }

    // Connect to server if not already connected
    if (server_socket_fd == -1) {
        if (connect_to_server() == -1) {
            errno = ECONNREFUSED;
            return -1;
        }
    }

    // Prepare the request header
    shm_header_t request_header;
    request_header.command = htonl(command);
    request_header.size = htonl(data_size);
    request_header.shmid = htonl(shmid);
    request_header.flags = htonl(flags);
    request_header.offset = htonl(offset);

    // Send the header
    if (send(server_socket_fd, &request_header, sizeof(request_header), 0) != sizeof(request_header)) {
        perror("send header");
        close(server_socket_fd);
        server_socket_fd = -1;
        errno = ECONNRESET;
        return -1;
    }

    // Send the data if there is any
    if (data && data_size > 0) {
        if (send(server_socket_fd, data, data_size, 0) != (ssize_t)data_size) {
            perror("send data");
            close(server_socket_fd);
            server_socket_fd = -1;
            errno = ECONNRESET;
            return -1;
        }
    }

    // Receive the response header
    shm_response_t response_header;
    if (recv(server_socket_fd, &response_header, sizeof(response_header), MSG_WAITALL) != sizeof(response_header)) {
        perror("recv response header");
        close(server_socket_fd);
        server_socket_fd = -1;
        errno = ECONNRESET;
        return -1;
    }

    // Convert from network to host byte order
    response_header.result = ntohl(response_header.result);
    response_header.error_code = ntohl(response_header.error_code);
    response_header.data_size = ntohl(response_header.data_size);

    // If command is read data, receive the data as well
    if (command == CMD_READ_DATA && response_header.data_size > 0) {
        *response_size = response_header.data_size;
        *response_data = malloc(response_header.data_size);
        if (*response_data == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (recv(server_socket_fd, *response_data, response_header.data_size, MSG_WAITALL) != (ssize_t)response_header.data_size) {
            perror("recv response data");
            free(*response_data);
            *response_data = NULL;
            close(server_socket_fd);
            server_socket_fd = -1;
            errno = ECONNRESET;
            return -1;
        }
    } else {
        *response_size = 0;
        *response_data = NULL;
    }

    // Map server error codes to POSIX errno values
    switch (response_header.error_code) {
        case SHM_EINVAL:
            errno = EINVAL;
            break;
        case SHM_ENOMEM:
            errno = ENOMEM;
            break;
        case SHM_EACCES:
            errno = EACCES;
            break;
        case SHM_ENOENT:
            errno = ENOENT;
            break;
        default:
            if (response_header.result < 0) {
                errno = EINVAL; // Default error
            }
            break;
    }

    return response_header.result;
}

// Initialize the client library
int distributed_shm_init(const char *server_host_param, int server_port_param) {
    if (server_host_param) {
        strncpy(server_host, server_host_param, sizeof(server_host) - 1);
        server_host[sizeof(server_host) - 1] = '\0';
    }
    
    if (server_port_param > 0 && server_port_param <= 65535) {
        server_port = server_port_param;
    }

    // Initialize the client segments array
    memset(client_segments, 0, sizeof(client_segments));
    
    client_initialized = 1;
    return 0;
}

// Cleanup the client library
void distributed_shm_cleanup(void) {
    // Detach from all attached segments
    for (int i = 0; i < MAX_CLIENT_SEGMENTS; i++) {
        if (client_segments[i].attached && client_segments[i].local_addr) {
            // We don't call shmdt here as it would try to lock the mutex again
            // Instead, we just mark as detached and free local memory
            if (client_segments[i].local_addr) {
                munmap(client_segments[i].local_addr, client_segments[i].size);
                client_segments[i].local_addr = NULL;
            }
        }
    }

    // Close the server connection
    if (server_socket_fd != -1) {
        close(server_socket_fd);
        server_socket_fd = -1;
    }
    
    client_initialized = 0;
}

// POSIX-compatible shmget function
int distributed_shmget(key_t key, size_t size, int shmflg) {
    if (!client_initialized) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&client_mutex);

    // Find an existing segment with this key or create a new one
    // For distributed SHM, we'll use the key as the shmid
    int shmid = (int)key;
    
    void *data = &size;
    size_t data_size = sizeof(size_t);
    void *response_data = NULL;
    size_t response_size = 0;

    int result = send_request_to_server(CMD_CREATE_SEGMENT, shmid, shmflg, 0, 
                                       data, data_size, &response_data, &response_size);

    if (response_data) {
        free(response_data);
    }

    if (result < 0) {
        pthread_mutex_unlock(&client_mutex);
        return -1;
    }

    // Store the segment info locally
    for (int i = 0; i < MAX_CLIENT_SEGMENTS; i++) {
        if (client_segments[i].shmid == 0 || client_segments[i].shmid == shmid) {
            client_segments[i].shmid = shmid;
            client_segments[i].size = size;
            client_segments[i].shmflg = shmflg;
            if (client_segments[i].local_addr == NULL) {
                // Create a local mapping area (though for distributed SHM, 
                // we'll handle data transfer differently)
                client_segments[i].local_addr = NULL; // We'll allocate on attach
            }
            break;
        }
    }

    pthread_mutex_unlock(&client_mutex);
    return shmid;
}

// POSIX-compatible shmat function
void *distributed_shmat(int shmid, const void *shmaddr, int shmflg) {
    if (!client_initialized || shmid < 0) {
        errno = EINVAL;
        return (void*)-1;
    }

    pthread_mutex_lock(&client_mutex);

    // Find the segment
    int segment_idx = -1;
    for (int i = 0; i < MAX_CLIENT_SEGMENTS; i++) {
        if (client_segments[i].shmid == shmid) {
            segment_idx = i;
            break;
        }
    }

    if (segment_idx == -1) {
        pthread_mutex_unlock(&client_mutex);
        errno = EINVAL;
        return (void*)-1;
    }

    // Send attach command to server
    void *response_data = NULL;
    size_t response_size = 0;
    int result = send_request_to_server(CMD_ATTACH_SEGMENT, shmid, shmflg, 0, 
                                       NULL, 0, &response_data, &response_size);

    if (response_data) {
        free(response_data);
    }

    if (result < 0) {
        pthread_mutex_unlock(&client_mutex);
        return (void*)-1;
    }

    // Update local segment state
    client_segments[segment_idx].attached = 1;
    client_segments[segment_idx].shmflg = shmflg;

    // Allocate local memory for this segment if not already done
    if (client_segments[segment_idx].local_addr == NULL) {
        client_segments[segment_idx].local_addr = mmap(NULL, client_segments[segment_idx].size,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (client_segments[segment_idx].local_addr == MAP_FAILED) {
            client_segments[segment_idx].local_addr = NULL;
            // Detach from server since we couldn't allocate local memory
            send_request_to_server(CMD_DETACH_SEGMENT, shmid, 0, 0, NULL, 0, NULL, NULL);
            pthread_mutex_unlock(&client_mutex);
            errno = ENOMEM;
            return (void*)-1;
        }
    }

    pthread_mutex_unlock(&client_mutex);
    
    // Return the local address where data will be stored/expected
    return client_segments[segment_idx].local_addr;
}

// POSIX-compatible shmdt function
int distributed_shmdt(const void *shmaddr) {
    if (!client_initialized || shmaddr == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&client_mutex);

    // Find the segment associated with this address
    int shmid = -1;
    int segment_idx = -1;
    
    for (int i = 0; i < MAX_CLIENT_SEGMENTS; i++) {
        if (client_segments[i].local_addr == shmaddr && client_segments[i].attached) {
            shmid = client_segments[i].shmid;
            segment_idx = i;
            break;
        }
    }

    if (shmid == -1 || segment_idx == -1) {
        pthread_mutex_unlock(&client_mutex);
        errno = EINVAL;
        return -1;
    }

    // Send detach command to server
    void *response_data = NULL;
    size_t response_size = 0;
    int result = send_request_to_server(CMD_DETACH_SEGMENT, shmid, 0, 0, 
                                       NULL, 0, &response_data, &response_size);

    if (response_data) {
        free(response_data);
    }

    if (result < 0) {
        pthread_mutex_unlock(&client_mutex);
        return -1;
    }

    // Update local segment state
    client_segments[segment_idx].attached = 0;

    pthread_mutex_unlock(&client_mutex);
    return 0;
}

// POSIX-compatible shmctl function
int distributed_shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    if (!client_initialized || shmid < 0) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&client_mutex);

    // Prepare data for certain commands
    void *data = NULL;
    size_t data_size = 0;
    
    if (cmd == IPC_SET && buf != NULL) {
        data = buf;
        data_size = sizeof(struct shmid_ds);
    } else if (cmd == IPC_STAT && buf != NULL) {
        // We'll fill the buffer after getting response from server
        data = buf;
        data_size = sizeof(struct shmid_ds);
    }

    void *response_data = NULL;
    size_t response_size = 0;
    int result = send_request_to_server(CMD_SHMCTL, shmid, cmd, 0, 
                                       data, data_size, &response_data, &response_size);

    // For IPC_STAT, copy the returned data to the user buffer
    if (cmd == IPC_STAT && buf != NULL && response_data != NULL && response_size >= sizeof(struct shmid_ds)) {
        memcpy(buf, response_data, sizeof(struct shmid_ds));
    }

    if (response_data) {
        free(response_data);
    }

    pthread_mutex_unlock(&client_mutex);
    if (result < 0) {
        return -1;
    }
    
    return result;
}

// Wrapper functions that match the standard POSIX names
int shmget(key_t key, size_t size, int shmflg) {
    return distributed_shmget(key, size, shmflg);
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    return distributed_shmat(shmid, shmaddr, shmflg);
}

int shmdt(const void *shmaddr) {
    return distributed_shmdt(shmaddr);
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    return distributed_shmctl(shmid, cmd, buf);
}
