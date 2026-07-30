/**
 * @file msg.h
 * @brief Shared message structures, constants, and POSIX IPC object names.
 * @author Toan Tran
 * @version 1.0.0
 */

#ifndef MSG_H
#define MSG_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
 * Constants
 * =================================================================== */

/** Maximum payload size for a single message (bytes) */
#define MAX_MSG_PAYLOAD 100

/** The message type for data chunks sent by sender to receiver */
#define SENDER_DATA_TYPE 1

/** The message type indicating the receiver has finished reading a chunk */
#define RECV_DONE_TYPE 2

/** The message type for file name transfer */
#define FILE_NAME_TRANSFER_TYPE 3

/** Maximum file name size (including null terminator) */
#define MAX_FILE_NAME_SIZE 100

/** Default shared memory chunk size */
#define DEFAULT_CHUNK_SIZE 4096

/** Minimum allowed chunk size */
#define MIN_CHUNK_SIZE 512

/** Maximum allowed chunk size */
#define MAX_CHUNK_SIZE (1024 * 1024)  /* 1 MB */

/** Default message queue timeout (seconds) */
#define DEFAULT_TIMEOUT_SEC 30

/* POSIX IPC object names */
#define SHM_NAME        "/ipc_shm"
#define MQ_SEND_NAME    "/ipc_mq_send"
#define MQ_ACK_NAME     "/ipc_mq_ack"

/* POSIX message queue attributes */
#define MQ_MAX_MSG      10
#define MQ_MSG_SIZE     sizeof(struct fileNameMsg)

/* ===================================================================
 * Data Structures
 * =================================================================== */

/**
 * @brief Message structure for transferring the file name from sender to receiver.
 */
struct fileNameMsg {
    /** The message type (should be FILE_NAME_TRANSFER_TYPE) */
    long mtype;
    /** The name of the file being transferred (null-terminated) */
    char fileName[MAX_FILE_NAME_SIZE];
};

/**
 * @brief Message structure sent from sender to receiver indicating
 *        that a chunk of data is ready in shared memory.
 */
struct message {
    /** The message type (should be SENDER_DATA_TYPE) */
    long mtype;
    /** Number of bytes ready in shared memory (0 signals end of transfer) */
    int size;
};

/**
 * @brief Message structure sent from receiver to sender acknowledging
 *        that a data chunk has been successfully read and saved.
 */
struct ackMessage {
    /** The message type (should be RECV_DONE_TYPE) */
    long mtype;
};

/**
 * @brief CRC32 checksum message sent at the end of a transfer.
 */
struct crcMessage {
    /** The message type */
    long mtype;
    /** The CRC32 checksum of the entire transferred data */
    uint32_t crc;
};

/**
 * @brief Program-wide version information.
 */
#define IPC_VERSION_MAJOR 1
#define IPC_VERSION_MINOR 0
#define IPC_VERSION_PATCH 0

#define IPC_VERSION_STRING "1.0.0"

#ifdef __cplusplus
}
#endif

#endif /* MSG_H */
