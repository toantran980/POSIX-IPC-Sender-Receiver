/**
 * @file sender.c
 * @brief POSIX IPC Sender - Transfers files to a receiver process.
 * @author Toan Tran
 * @version 1.0.0
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>

#include "msg.h"
#include "log.h"
#include "ipc_utils.h"
#include "crc32.h"

/* ===================================================================
 * Global State
 * =================================================================== */

static int g_shm_fd = -1;
static mqd_t g_mq_send_fd = (mqd_t)-1;
static mqd_t g_mq_ack_fd = (mqd_t)-1;
static void *g_sharedMemPtr = NULL;
static size_t g_chunk_size = DEFAULT_CHUNK_SIZE;
static bool g_connected = false;

/* ===================================================================
 * Signal Handler
 * =================================================================== */

static void handle_signal(int sig)
{
    const char *sig_name = (sig == SIGINT) ? "SIGINT" : "SIGTERM";
    log_warn("Received %s, cleaning up...", sig_name);

    if (g_connected) {
        ipc_cleanup_client(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                           g_sharedMemPtr, g_chunk_size);
        g_connected = false;
    }
    log_shutdown();
    _exit(128 + sig);
}

/* ===================================================================
 * Command-Line Options
 * =================================================================== */

static void print_usage(const char *prog_name)
{
    fprintf(stderr,
        "Usage: %s [options] <filename>\n"
        "\n"
        "Options:\n"
        "  -c, --chunk-size <bytes>   Chunk size (default: %d)\n"
        "  -v, --verbose              Verbose output\n"
        "  -q, --quiet                Suppress non-error output\n"
        "      --log-file <path>      Write logs to file\n"
        "      --help                 Show this help\n"
        "      --version              Show version\n",
        prog_name, DEFAULT_CHUNK_SIZE);
}

static void print_version(void)
{
    printf("sender (POSIX IPC) v%s\n", IPC_VERSION_STRING);
}

/* ===================================================================
 * File Validation
 * =================================================================== */

static int validate_input_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1) {
        log_error("Cannot access '%s': %s", path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        log_error("'%s' is not a regular file", path);
        return -1;
    }
    if (access(path, R_OK) == -1) {
        log_error("No read permission for '%s'", path);
        return -1;
    }
    return 0;
}

/* ===================================================================
 * File Name Transfer
 * =================================================================== */

static int send_file_name(const char *fileName)
{
    log_info("Sending file name: %s", fileName);

    size_t name_len = strlen(fileName);
    if (name_len >= MAX_FILE_NAME_SIZE) {
        log_error("File name too long");
        return -1;
    }

    struct fileNameMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = FILE_NAME_TRANSFER_TYPE;
    memcpy(msg.fileName, fileName, name_len);
    msg.fileName[name_len] = '\0';

    if (mq_send(g_mq_send_fd, (const char *)&msg, sizeof(msg), 0) == -1) {
        log_error("Failed to send file name: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ===================================================================
 * File Transfer
 * =================================================================== */

static unsigned long long transfer_file(const char *fileName)
{
    FILE *fp = fopen(fileName, "rb");
    if (!fp) {
        log_error("Cannot open '%s'", fileName);
        return 0;
    }

    struct stat st;
    stat(fileName, &st);
    unsigned long long file_size = (unsigned long long)st.st_size;
    unsigned long long total_sent = 0;
    uint32_t crc = 0;

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1) {
        size_t bytes_read = fread(g_sharedMemPtr, 1, g_chunk_size, fp);
        if (bytes_read == 0) {
            if (ferror(fp)) {
                log_error("Error reading file: %s", strerror(errno));
                fclose(fp);
                return 0;
            }
            break;
        }

        crc = crc32_update(crc, g_sharedMemPtr, bytes_read);

        struct message msg;
        memset(&msg, 0, sizeof(msg));
        msg.mtype = SENDER_DATA_TYPE;
        msg.size = (int)bytes_read;

        if (mq_send(g_mq_send_fd, (const char *)&msg, sizeof(msg), 0) == -1) {
            log_error("mq_send failed: %s", strerror(errno));
            fclose(fp);
            return 0;
        }

        total_sent += bytes_read;

        if (file_size > 0) {
            unsigned long long pct = total_sent * 100 / file_size;
            if (pct > ((total_sent - bytes_read) * 100 / file_size)) {
                clock_gettime(CLOCK_MONOTONIC, &current_time);
                double elapsed = (double)(current_time.tv_sec - start_time.tv_sec)
                               + (double)(current_time.tv_nsec - start_time.tv_nsec) / 1e9;
                double speed = (elapsed > 0) ? (double)total_sent / elapsed : 0;
                log_info("Progress: %llu%% (%llu/%llu bytes, %.1f KB/s)",
                         pct, total_sent, file_size, speed / 1024.0);
            }
        }

        struct ackMessage ack;
        if (mq_receive(g_mq_ack_fd, (char *)&ack, sizeof(ack), NULL) == -1) {
            log_error("mq_receive (ack) failed: %s", strerror(errno));
            fclose(fp);
            return 0;
        }
    }

    /* Signal end of data */
    struct message done_msg;
    memset(&done_msg, 0, sizeof(done_msg));
    done_msg.mtype = SENDER_DATA_TYPE;
    done_msg.size = 0;
    mq_send(g_mq_send_fd, (const char *)&done_msg, sizeof(done_msg), 0);

    /* Send CRC32 */
    struct crcMessage crc_msg;
    memset(&crc_msg, 0, sizeof(crc_msg));
    crc_msg.mtype = SENDER_DATA_TYPE;
    crc_msg.crc = crc;
    mq_send(g_mq_send_fd, (const char *)&crc_msg, sizeof(crc_msg), 0);

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    double elapsed = (double)(current_time.tv_sec - start_time.tv_sec)
                   + (double)(current_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double speed = (elapsed > 0) ? (double)total_sent / elapsed : 0;
    log_info("Transfer complete: %llu bytes in %.1fs (%.1f KB/s)",
             total_sent, elapsed, speed / 1024.0);

    fclose(fp);
    return total_sent;
}

/* ===================================================================
 * Main
 * =================================================================== */

int main(int argc, char **argv)
{
    int log_level = LOG_LEVEL_INFO;
    bool quiet = false;
    const char *log_file = NULL;
    const char *filename = NULL;

    static struct option long_opts[] = {
        {"chunk-size", required_argument, NULL, 'c'},
        {"verbose",    no_argument,       NULL, 'v'},
        {"quiet",      no_argument,       NULL, 'q'},
        {"log-file",   required_argument, NULL, 1},
        {"help",       no_argument,       NULL, 2},
        {"version",    no_argument,       NULL, 3},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:vq", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'c': {
                long val = atol(optarg);
                if (val < MIN_CHUNK_SIZE || val > MAX_CHUNK_SIZE) {
                    fprintf(stderr, "Chunk size must be %d-%d\n",
                            MIN_CHUNK_SIZE, MAX_CHUNK_SIZE);
                    return EXIT_FAILURE;
                }
                g_chunk_size = (size_t)val;
                break;
            }
            case 'v': log_level = LOG_LEVEL_DEBUG; break;
            case 'q': quiet = true; log_level = LOG_LEVEL_WARN; break;
            case 1:   log_file = optarg; break;
            case 2:   print_usage(argv[0]); return EXIT_SUCCESS;
            case 3:   print_version(); return EXIT_SUCCESS;
            default:  print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "ERROR: No input file specified\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    filename = argv[optind];

    if (log_init(log_level, log_file, quiet) != 0)
        return EXIT_FAILURE;

    log_info("Sender v%s starting", IPC_VERSION_STRING);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (validate_input_file(filename) != 0) {
        log_shutdown();
        return EXIT_FAILURE;
    }

    if (ipc_init_sender(&g_shm_fd, &g_mq_send_fd, &g_mq_ack_fd,
                        &g_sharedMemPtr, g_chunk_size) != 0) {
        log_shutdown();
        return EXIT_FAILURE;
    }
    g_connected = true;

    if (send_file_name(filename) != 0) {
        ipc_cleanup_client(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                           g_sharedMemPtr, g_chunk_size);
        g_connected = false;
        log_shutdown();
        return EXIT_FAILURE;
    }

    unsigned long long bytes = transfer_file(filename);
    ipc_cleanup_client(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                       g_sharedMemPtr, g_chunk_size);
    g_connected = false;

    if (bytes == 0) {
        log_error("Transfer failed");
        log_shutdown();
        return EXIT_FAILURE;
    }

    log_info("Sender finished: %llu bytes", bytes);
    log_shutdown();
    fprintf(stdout, "%llu\n", bytes);
    return EXIT_SUCCESS;
}
