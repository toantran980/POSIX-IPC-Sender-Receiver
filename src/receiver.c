/**
 * @file receiver.c
 * @brief POSIX IPC Receiver - Receives files from a sender process.
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

static int g_shm_fd = -1;
static mqd_t g_mq_send_fd = (mqd_t)-1;
static mqd_t g_mq_ack_fd = (mqd_t)-1;
static void *g_sharedMemPtr = NULL;
static size_t g_chunk_size = DEFAULT_CHUNK_SIZE;
static bool g_connected = false;
static bool g_force = false;
static const char *g_output_path = NULL;

static void handle_signal(int sig)
{
    const char *sig_name = (sig == SIGINT) ? "SIGINT" : "SIGTERM";
    log_warn("Received %s, cleaning up IPC resources...", sig_name);
    if (g_connected) {
        ipc_cleanup(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                    g_sharedMemPtr, g_chunk_size);
        g_connected = false;
    }
    log_shutdown();
    _exit(128 + sig);
}

static void print_usage(const char *prog_name)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -o, --output <path>        Output file path\n"
        "  -f, --force                Overwrite existing file\n"
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
    printf("receiver (POSIX IPC) v%s\n", IPC_VERSION_STRING);
}

static int recv_file_name(char *fileName, int maxSize)
{
    log_info("Waiting for file name from sender...");
    struct fileNameMsg msg;
    ssize_t recv_len = mq_receive(g_mq_send_fd, (char *)&msg, sizeof(msg), NULL);
    if (recv_len == -1) {
        log_error("Failed to receive file name: %s", strerror(errno));
        return -1;
    }
    msg.fileName[MAX_FILE_NAME_SIZE - 1] = '\0';
    snprintf(fileName, (size_t)maxSize, "%s", msg.fileName);
    log_info("Received file name: %s", fileName);
    return 0;
}

static void make_output_path(char *recvFileName, size_t maxLen, const char *origName)
{
    if (g_output_path != NULL) {
        snprintf(recvFileName, maxLen, "%s", g_output_path);
        return;
    }
    const char *dot = strrchr(origName, '.');
    if (dot != NULL && dot != origName) {
        size_t base_len = (size_t)(dot - origName);
        if (base_len + 7 + strlen(dot) < maxLen) {
            memcpy(recvFileName, origName, base_len);
            memcpy(recvFileName + base_len, "__recv", 6);
            memcpy(recvFileName + base_len + 6, dot, strlen(dot) + 1);
        } else {
            snprintf(recvFileName, maxLen, "%s__recv", origName);
        }
    } else {
        snprintf(recvFileName, maxLen, "%s__recv", origName);
    }
}

static unsigned long long receive_file(const char *recvFileName)
{
    if (!g_force && access(recvFileName, F_OK) == 0) {
        log_error("Output file '%s' exists. Use -f to overwrite.", recvFileName);
        return 0;
    }

    FILE *fp = fopen(recvFileName, "wb");
    if (!fp) {
        log_error("Cannot open '%s': %s", recvFileName, strerror(errno));
        return 0;
    }

    log_info("Writing to: %s", recvFileName);
    unsigned long long total_recv = 0;
    uint32_t computed_crc = 0;
    uint32_t sender_crc = 0;
    bool got_sender_crc = false;

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1) {
        struct message msg;
        ssize_t recv_len = mq_receive(g_mq_send_fd, (char *)&msg, sizeof(msg), NULL);
        if (recv_len == -1) {
            log_error("mq_receive failed: %s", strerror(errno));
            fclose(fp);
            return 0;
        }

        int msg_size = msg.size;
        if (msg_size == 0) {
            log_info("End-of-transfer signal received");
            struct crcMessage crc_msg;
            struct timespec timeout_ts;
            ipc_timeout_to_timespec(2, &timeout_ts);
            ssize_t crc_len = mq_timedreceive(g_mq_send_fd, (char *)&crc_msg,
                                               sizeof(crc_msg), NULL, &timeout_ts);
            if (crc_len >= (ssize_t)(sizeof(crc_msg.mtype) + sizeof(crc_msg.crc))) {
                sender_crc = crc_msg.crc;
                got_sender_crc = true;
                log_info("Received CRC32: 0x%08X", sender_crc);
            } else {
                log_warn("No CRC32 received from sender");
            }
            break;
        }

        size_t written = fwrite(g_sharedMemPtr, 1, (size_t)msg_size, fp);
        if (written != (size_t)msg_size) {
            log_error("fwrite error: wrote %zu of %d bytes", written, msg_size);
            fclose(fp);
            return 0;
        }

        computed_crc = crc32_update(computed_crc, g_sharedMemPtr, (size_t)msg_size);
        total_recv += (unsigned long long)msg_size;

        struct ackMessage ack;
        memset(&ack, 0, sizeof(ack));
        ack.mtype = RECV_DONE_TYPE;
        if (mq_send(g_mq_ack_fd, (const char *)&ack, sizeof(ack), 0) == -1) {
            log_error("Failed to send ack: %s", strerror(errno));
            fclose(fp);
            return 0;
        }

        if (total_recv % (g_chunk_size * 100) < (unsigned long long)msg_size) {
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            double elapsed = (double)(current_time.tv_sec - start_time.tv_sec)
                           + (double)(current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            double speed = (elapsed > 0) ? (double)total_recv / elapsed : 0;
            log_info("Received: %llu bytes (%.1f KB/s)", total_recv, speed / 1024.0);
        }
    }

    fclose(fp);

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    double elapsed = (double)(current_time.tv_sec - start_time.tv_sec)
                   + (double)(current_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double speed = (elapsed > 0) ? (double)total_recv / elapsed : 0;
    log_info("Reception complete: %llu bytes in %.1fs (%.1f KB/s)",
             total_recv, elapsed, speed / 1024.0);

    if (got_sender_crc) {
        if (computed_crc == sender_crc) {
            log_info("CRC32 MATCH (0x%08X) - integrity verified", computed_crc);
        } else {
            log_error("CRC32 MISMATCH! Computed: 0x%08X, Sender: 0x%08X",
                      computed_crc, sender_crc);
        }
    } else {
        log_warn("No CRC32 verification performed");
    }

    return total_recv;
}

int main(int argc, char **argv)
{
    int log_level = LOG_LEVEL_INFO;
    bool quiet = false;
    const char *log_file = NULL;

    static struct option long_opts[] = {
        {"output",     required_argument, NULL, 'o'},
        {"force",      no_argument,       NULL, 'f'},
        {"chunk-size", required_argument, NULL, 'c'},
        {"verbose",    no_argument,       NULL, 'v'},
        {"quiet",      no_argument,       NULL, 'q'},
        {"log-file",   required_argument, NULL, 1},
        {"help",       no_argument,       NULL, 2},
        {"version",    no_argument,       NULL, 3},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "o:fc:vq", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'o': g_output_path = optarg; break;
            case 'f': g_force = true; break;
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

    if (log_init(log_level, log_file, quiet) != 0)
        return EXIT_FAILURE;

    log_info("Receiver v%s starting", IPC_VERSION_STRING);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (ipc_init_receiver(&g_shm_fd, &g_mq_send_fd, &g_mq_ack_fd,
                          &g_sharedMemPtr, g_chunk_size) != 0) {
        log_shutdown();
        return EXIT_FAILURE;
    }
    g_connected = true;

    char fileName[MAX_FILE_NAME_SIZE];
    if (recv_file_name(fileName, MAX_FILE_NAME_SIZE) != 0) {
        ipc_cleanup(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                    g_sharedMemPtr, g_chunk_size);
        g_connected = false;
        log_shutdown();
        return EXIT_FAILURE;
    }

    char recvFileName[512];
    make_output_path(recvFileName, sizeof(recvFileName), fileName);
    log_info("Converting: %s -> %s", fileName, recvFileName);

    unsigned long long bytes = receive_file(recvFileName);
    ipc_cleanup(g_shm_fd, g_mq_send_fd, g_mq_ack_fd,
                g_sharedMemPtr, g_chunk_size);
    g_connected = false;

    if (bytes == 0) {
        log_error("Reception failed");
        log_shutdown();
        return EXIT_FAILURE;
    }

    log_info("Receiver finished: %llu bytes", bytes);
    log_shutdown();
    fprintf(stdout, "%llu\n", bytes);
    return EXIT_SUCCESS;
}
