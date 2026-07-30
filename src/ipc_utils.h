/**
 * @file ipc_utils.h
 * @brief Shared POSIX IPC utilities.
 * @author Toan Tran
 * @version 1.0.0
 */

#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <mqueue.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipc_init_receiver(int *shm_fd_ptr, mqd_t *mq_send_ptr, mqd_t *mq_ack_ptr,
                      void **sharedMemPtr, size_t chunk_size);

int ipc_init_sender(int *shm_fd_ptr, mqd_t *mq_send_ptr, mqd_t *mq_ack_ptr,
                    void **sharedMemPtr, size_t chunk_size);

int ipc_cleanup(int shm_fd, mqd_t mq_send_fd, mqd_t mq_ack_fd,
                void *sharedMemPtr, size_t chunk_size);

int ipc_cleanup_client(int shm_fd, mqd_t mq_send_fd, mqd_t mq_ack_fd,
                       void *sharedMemPtr, size_t chunk_size);

void ipc_msleep(unsigned long ms);
void ipc_timeout_to_timespec(unsigned int seconds, struct timespec *out_ts);

#ifdef __cplusplus
}
#endif

#endif /* IPC_UTILS_H */
