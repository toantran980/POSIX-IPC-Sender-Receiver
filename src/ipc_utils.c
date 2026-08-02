/**
 * @file ipc_utils.c
 * @brief Shared POSIX IPC utilities implementation.
 * @author Toan Tran
 * @version 1.0.0
 */

#define _POSIX_C_SOURCE 200112L

#include "msg.h"
#include "ipc_utils.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

static int shm_open_shared(const char *name, int flags, mode_t mode) {
    int fd = shm_open(name, flags, mode);
    if (fd == -1) {
        log_error("shm_open('%s'): %s", name, strerror(errno));
    }
    return fd;
}

static mqd_t mq_open_shared(const char *name, int flags, mode_t mode,
                            struct mq_attr *attr) {
    mqd_t mqd = mq_open(name, flags, mode, attr);
    if (mqd == (mqd_t)-1) {
        log_error("mq_open('%s'): %s", name, strerror(errno));
    }
    return mqd;
}

int ipc_init_receiver(int *shm_fd_ptr, mqd_t *mq_send_ptr, mqd_t *mq_ack_ptr,
                      void **sharedMemPtr, size_t chunk_size)
{
    log_info("Initializing receiver (creator of IPC resources)...");

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAX_MSG;
    attr.mq_msgsize = MQ_MSG_SIZE;
    attr.mq_curmsgs = 0;

    *shm_fd_ptr = shm_open_shared(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (*shm_fd_ptr == -1) return -1;

    if (ftruncate(*shm_fd_ptr, (off_t)chunk_size) == -1) {
        log_error("ftruncate: %s", strerror(errno));
        shm_unlink(SHM_NAME);
        return -1;
    }

    *sharedMemPtr = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, *shm_fd_ptr, 0);
    if (*sharedMemPtr == MAP_FAILED) {
        log_error("mmap: %s", strerror(errno));
        shm_unlink(SHM_NAME);
        return -1;
    }

    *mq_send_ptr = mq_open_shared(MQ_SEND_NAME, O_CREAT | O_RDWR,
                                  S_IRUSR | S_IWUSR, &attr);
    if (*mq_send_ptr == (mqd_t)-1) {
        munmap(*sharedMemPtr, chunk_size);
        shm_unlink(SHM_NAME);
        return -1;
    }

    *mq_ack_ptr = mq_open_shared(MQ_ACK_NAME, O_CREAT | O_RDWR,
                                 S_IRUSR | S_IWUSR, &attr);
    if (*mq_ack_ptr == (mqd_t)-1) {
        mq_close(*mq_send_ptr);
        mq_unlink(MQ_SEND_NAME);
        munmap(*sharedMemPtr, chunk_size);
        shm_unlink(SHM_NAME);
        return -1;
    }

    log_info("IPC resources created (chunk_size=%zu)", chunk_size);
    return 0;
}

int ipc_init_sender(int *shm_fd_ptr, mqd_t *mq_send_ptr, mqd_t *mq_ack_ptr,
                    void **sharedMemPtr, size_t chunk_size) {
    log_info("Initializing sender (connecting to IPC)...");

    *shm_fd_ptr = shm_open_shared(SHM_NAME, O_RDWR, S_IRUSR | S_IWUSR);
    if (*shm_fd_ptr == -1) return -1;

    *sharedMemPtr = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, *shm_fd_ptr, 0);
    if (*sharedMemPtr == MAP_FAILED) {
        log_error("mmap: %s", strerror(errno));
        return -1;
    }

    *mq_send_ptr = mq_open_shared(MQ_SEND_NAME, O_RDWR, 0, NULL);
    if (*mq_send_ptr == (mqd_t)-1) {
        munmap(*sharedMemPtr, chunk_size);
        return -1;
    }

    *mq_ack_ptr = mq_open_shared(MQ_ACK_NAME, O_RDWR, 0, NULL);
    if (*mq_ack_ptr == (mqd_t)-1) {
        mq_close(*mq_send_ptr);
        munmap(*sharedMemPtr, chunk_size);
        return -1;
    }

    log_info("Connected to IPC resources");
    return 0;
}

int ipc_cleanup(int shm_fd, mqd_t mq_send_fd, mqd_t mq_ack_fd,
                void *sharedMemPtr, size_t chunk_size) {
    int ret = 0;
    (void)shm_fd;

    if (sharedMemPtr != NULL && sharedMemPtr != MAP_FAILED) {
        if (munmap(sharedMemPtr, chunk_size) == -1) {
            log_error("munmap: %s", strerror(errno));
            ret = -1;
        }
    }
    if (mq_send_fd != (mqd_t)-1 && mq_close(mq_send_fd) == -1) {
        log_error("mq_close(send): %s", strerror(errno));
        ret = -1;
    }
    if (mq_ack_fd != (mqd_t)-1 && mq_close(mq_ack_fd) == -1) {
        log_error("mq_close(ack): %s", strerror(errno));
        ret = -1;
    }
    if (shm_unlink(SHM_NAME) == -1) {
        log_error("shm_unlink: %s", strerror(errno));
        ret = -1;
    }
    if (mq_unlink(MQ_SEND_NAME) == -1) {
        log_error("mq_unlink(send): %s", strerror(errno));
        ret = -1;
    }
    if (mq_unlink(MQ_ACK_NAME) == -1) {
        log_error("mq_unlink(ack): %s", strerror(errno));
        ret = -1;
    }
    if (ret == 0) log_info("IPC resources cleaned up");
    return ret;
}

int ipc_cleanup_client(int shm_fd, mqd_t mq_send_fd, mqd_t mq_ack_fd,
                       void *sharedMemPtr, size_t chunk_size) {
    int ret = 0;
    (void)shm_fd;

    if (sharedMemPtr != NULL && sharedMemPtr != MAP_FAILED) {
        if (munmap(sharedMemPtr, chunk_size) == -1) {
            log_error("munmap: %s", strerror(errno));
            ret = -1;
        }
    }
    if (mq_send_fd != (mqd_t)-1 && mq_close(mq_send_fd) == -1) {
        log_error("mq_close(send): %s", strerror(errno));
        ret = -1;
    }
    if (mq_ack_fd != (mqd_t)-1 && mq_close(mq_ack_fd) == -1) {
        log_error("mq_close(ack): %s", strerror(errno));
        ret = -1;
    }
    if (ret == 0) log_info("Sender disconnected");
    return ret;
}

void ipc_msleep(unsigned long ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

void ipc_timeout_to_timespec(unsigned int seconds, struct timespec *out_ts) {
    clock_gettime(CLOCK_REALTIME, out_ts);
    out_ts->tv_sec += (time_t)seconds;
}
