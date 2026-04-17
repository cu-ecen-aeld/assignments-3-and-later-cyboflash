#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

// Configuration constants
#define FILE_NAME           "aesdsocketdata"
#define FOLDER_PATH         "/var/tmp"
#define DATA_FILE           (FOLDER_PATH "/" FILE_NAME)

#define CHUNK_SIZE_BYTES    ((uint16_t)512)
#define END_OF_PACKET       ((char)'\n')
#define MAX_NUM_CONNECTIONS ((int)1)
#define PORT                ((const char *)"9000")
#define RET_ERR             ((int)-1)
#define RET_INTR            ((int)1)
#define RET_SUCCESS         ((int)0)

// Function prototypes
void cleanup(int sockfd, struct addrinfo *res, FILE *fp, uint8_t *accumulator);
void client_cleanup(int *client_sockfd, char *client_host, uint8_t **accumulator, size_t *accumulator_size);
int setup(int *sockfd, struct addrinfo **res, const char *program_name);
int recv_packet(int sockfd, int *client_sockfd,
                char *client_host, uint8_t **accumulator,
                size_t *accumulator_size, uint8_t *chunk);
int send_file(int client_sockfd, uint8_t *accumulator,
              size_t accumulator_size, uint8_t *chunk);
void handle_shutdown(int signum);

volatile static __sig_atomic_t is_done = 0;

int main(int argc, char** argv)
{
    int sockfd = -1;
    struct addrinfo *res = NULL;
    uint8_t *accumulator = NULL;
    size_t accumulator_size = 0;
    uint8_t chunk[CHUNK_SIZE_BYTES];
    int opt = 0;
    bool is_daemon = false;

    struct option long_options[] = {
        {"daemon", no_argument, NULL, 'd'},
        {0, 0, 0, 0},
    };

    while (-1 != (opt = getopt_long(argc, argv, "d", long_options, NULL))) {
        switch (opt) {
            case 'd':
                is_daemon = true;
                break;
            case '?':
            default:
                fprintf(stderr, "Usage: %s [-d|--daemon]\n", argv[0]);
                return RET_ERR;
        }
    }

    if (is_daemon) {
        pid_t pid = fork();

        // Parent process
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        else if (-1 == pid) {
            fprintf(stderr, "Unable to fork a process");
            exit(EXIT_FAILURE);
        }
    }

    // fork() succeeded. We are in a child process

    // Detach ourselves from the controlling terminal
    // and parent group id and createa a new session
    // while becoming a new process group leader
    setsid();

    // Extract the program name from the path for cleaner logging
    char *program_name = strrchr(argv[0], '/');
    program_name = (program_name == NULL) ? argv[0] : program_name + 1;

    // Initialize networking: create directory, open log, socket, bind, and listen
    if (RET_ERR == setup(&sockfd, &res, program_name)) {
        return RET_ERR;
    }

    syslog(LOG_DEBUG, "Setup completed");

    // Main connection loop: accept connections sequentially
    int client_sockfd = -1;
    char client_host[NI_MAXHOST] = {0};
    while (0 == is_done) {
        // Accept a connection and receive data until end of packet marker
        // is found
        syslog(LOG_DEBUG, "Waiting on a packet...");
        int ret = recv_packet(sockfd, &client_sockfd,
                              client_host, &accumulator,
                              &accumulator_size, chunk);
        if (RET_ERR == ret) {
            cleanup(sockfd, res, NULL, accumulator);
            return RET_ERR;
        }
        else if (RET_INTR == ret) {
            continue;
        }

        syslog(LOG_DEBUG, "Packet received");

        // Append received data to the file and send the full file back to the client
        syslog(LOG_DEBUG, "Sending a response ...");
        if (RET_ERR == send_file(client_sockfd, accumulator,
                                 accumulator_size, chunk)) {
            client_cleanup(&client_sockfd, client_host,
                           &accumulator, &accumulator_size);
            cleanup(sockfd, res, NULL, accumulator);
            return RET_ERR;
        }

        // Gracefully do cleanup for the client after end of packet was received and processed
        client_cleanup(&client_sockfd, client_host, &accumulator, &accumulator_size);
    }

    syslog(LOG_DEBUG, "Cleanup ...");

    // Gracefully do cleanup for the client when while loop was exited
    // due to is_done being set to 1
    client_cleanup(&client_sockfd, client_host, &accumulator, &accumulator_size);
    cleanup(sockfd, res, NULL, accumulator);

    if (-1 == remove(DATA_FILE)) {
        syslog(LOG_ERR, "Unable to remove %s: %s", DATA_FILE, strerror(errno));
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    return RET_SUCCESS;
}

void handle_shutdown(int signum) {
    if (SIGINT == signum || SIGTERM == signum) {
        is_done = 1;
    }
}

/**
 * Handles client cleanup
 */
void client_cleanup(int *client_sockfd, char *client_host,
                    uint8_t **accumulator, size_t *accumulator_size) {
    if (client_sockfd != NULL && *client_sockfd != -1) {
        if (-1 == close(*client_sockfd)) {
            syslog(LOG_ERR, "close failed: %s", strerror(errno));
        }
        *client_sockfd = -1;
        if (client_host != NULL) {
            syslog(LOG_INFO, "Closed connection from %s", client_host);
        }
    }

    if (accumulator != NULL && *accumulator != NULL) {
        free(*accumulator);
        *accumulator = NULL;
    }

    if (accumulator_size != NULL) {
        *accumulator_size = 0;
    }
}

/**
 * Handles cleanup
 */
void cleanup(int sockfd, struct addrinfo *res, FILE *fp, uint8_t *accumulator) {
    if (-1 != sockfd) {
        if (-1 == close(sockfd)) {
            syslog(LOG_ERR, "close failed: %s", strerror(errno));
        }
    }
    if (NULL != res) {
        freeaddrinfo(res);
    }
    if (NULL != fp) {
        if (0 != fclose(fp)) {
            syslog(LOG_ERR, "fclose failed: %s", strerror(errno));
        }
    }
    free(accumulator);
    closelog();
}

/**
 * Performs initial setup: directory creation, address configuration,
 * and socket listener initialization.
 */
int setup(int *sockfd, struct addrinfo **res, const char *program_name) {
    int status;
    struct addrinfo hints = {0};
    struct sigaction sa;

    openlog(program_name, LOG_PID | LOG_PERROR, LOG_DAEMON);

    // Allow all messages
    setlogmask(LOG_UPTO(LOG_DEBUG));

    // Setup SIGTERM and SIGINT handler
    memset(&sa, 0, sizeof(sa));
    // Block every signal while signal handler is execuring
    sigfillset(&sa.sa_mask);

    sa.sa_handler = handle_shutdown;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);


    // Ensure the temporary directory exists for the data file
    DIR *dir = opendir(FOLDER_PATH);
    if (NULL != dir) {
        closedir(dir);
    } else if (ENOENT == errno) {
        if (-1 == mkdir(FOLDER_PATH, 0755)) {
            syslog(LOG_ERR, "mkdir failed: %s", strerror(errno));
            return RET_ERR;
        }
    } else {
        syslog(LOG_ERR, "opendir failed: %s", strerror(errno));
        return RET_ERR;
    }

    // Prepare address structures for binding to PORT 9000
    hints.ai_family   = AF_INET;     // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP Socket
    hints.ai_flags    = AI_PASSIVE;  // Bind to all interfaces

    status = getaddrinfo(NULL, PORT, &hints, res);
    if (0 != status) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(status));
        return RET_ERR;
    }

    // Create the listener socket
    syslog(LOG_DEBUG, "Creating a server socket...");
    *sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (-1 == *sockfd) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        freeaddrinfo(*res);
        *res = NULL;
        return RET_ERR;
    }

    // Enable SO_REUSEADDR to avoid "Address already in use" errors during quick restarts
    int yes = 1;
    if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
    }

    // Bind the socket to the port
    syslog(LOG_DEBUG, "Binding socket to a port ...");
    status = bind(*sockfd, (*res)->ai_addr, (*res)->ai_addrlen);
    if (-1 == status) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        if (-1 == close(*sockfd)) {
            syslog(LOG_ERR, "close failed: %s", strerror(errno));
        }
        *sockfd = -1;
        freeaddrinfo(*res);
        *res = NULL;
        return RET_ERR;
    }

    // Start listening for incoming connections
    syslog(LOG_DEBUG, "Starting to listen ...");
    status = listen(*sockfd, MAX_NUM_CONNECTIONS);
    if (-1 == status) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        if (-1 == close(*sockfd)) {
            syslog(LOG_ERR, "close failed: %s", strerror(errno));
        }
        *sockfd = -1;
        freeaddrinfo(*res);
        *res = NULL;
        return RET_ERR;
    }

    return RET_SUCCESS;
}

/**
 * Accepts a connection and receives data into a dynamic accumulator
 * until the END_OF_PACKET character (\n) is received.
 */
int recv_packet(int sockfd, int *client_sockfd,
                char *client_host, uint8_t **accumulator,
                size_t *accumulator_size, uint8_t *chunk) {
    struct sockaddr_storage client_addr = {0};
    socklen_t client_socklen = sizeof(client_addr);

    // Accept an incoming connection
    *client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr,
                            &client_socklen);
    if (-1 == *client_sockfd) {
        // interrupted by a signal
        if (EINTR == errno) {
            return RET_INTR;
        }
        syslog(LOG_ERR, "accept failed: %s", strerror(errno));
        return RET_ERR;
    }

    // Resolve and log the client's IP address
    int status = getnameinfo((struct sockaddr *)&client_addr,
                             client_socklen,
                             client_host,
                             NI_MAXHOST,
                             NULL, 0, NI_NUMERICHOST);
    if (0 != status) {
        syslog(LOG_ERR, "getnameinfo failed: %s", gai_strerror(status));
    } else {
        syslog(LOG_INFO, "Accepted connection from %s", client_host);
    }

    // Read data from the socket until the packet terminator is found
    bool is_end_of_packet = false;
    while (!is_end_of_packet) {
        ssize_t bytes_received = recv(*client_sockfd, chunk, CHUNK_SIZE_BYTES, 0);
        if (-1 == bytes_received) {
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            if (-1 == close(*client_sockfd)) {
                syslog(LOG_ERR, "close failed: %s", strerror(errno));
            }
            return RET_ERR;
        }
        if (0 == bytes_received) {
            break; // Connection closed by peer
        }

        // Expand the accumulator buffer to accommodate new data
        uint8_t* tmp_ptr = realloc(*accumulator, *accumulator_size + bytes_received);
        if (NULL == tmp_ptr) {
            syslog(LOG_ERR, "realloc failed: %s", strerror(errno));
            if (-1 == close(*client_sockfd)) {
                syslog(LOG_ERR, "close failed: %s", strerror(errno));
            }
            return RET_ERR;
        }
        *accumulator = tmp_ptr;
        memcpy(*accumulator + *accumulator_size, chunk, bytes_received);
        *accumulator_size += bytes_received;

        // Check if the current chunk contains the end-of-packet marker
        if (memchr(chunk, END_OF_PACKET, bytes_received)) {
            is_end_of_packet = true;
        }
    }

    return RET_SUCCESS;
}

/**
 * Appends the received packet to the data file and sends the entire
 * content of the file back to the client, reusing the accumulator as a buffer.
 */
int send_file(int client_sockfd, uint8_t *accumulator,
              size_t accumulator_size, uint8_t *chunk) {
    int ret = RET_SUCCESS;
    // Open the file in append and read mode
    FILE *fp = fopen(DATA_FILE, "a+");
    if (NULL == fp) {
        syslog(LOG_ERR, "fopen failed: %s", strerror(errno));
        return RET_ERR;
    }

    // Write the latest packet to the end of the file
    if (accumulator_size > 0) {
        if (fwrite(accumulator, 1, accumulator_size, fp) != accumulator_size) {
            syslog(LOG_ERR, "fwrite failed");
            ret = RET_ERR;
            goto cleanup;
        }
        if (0 != fflush(fp)) {
            syslog(LOG_ERR, "fflush failed: %s", strerror(errno));
            ret = RET_ERR;
            goto cleanup;
        }
    }

    // Rewind to the beginning of the file to send full content
    rewind(fp);

    // Buffer to use for reading and sending
    uint8_t *send_buffer;

    // Use the accumulator if it is large enough, otherwise use the re-used chunk buffer
    if (accumulator_size > CHUNK_SIZE_BYTES) {
        send_buffer = accumulator;
    } else {
        send_buffer = chunk;
    }

    // Read the file chunk-by-chunk and send to the client
    size_t bytes_read = fread(send_buffer, 1, CHUNK_SIZE_BYTES, fp);
    while (bytes_read > 0) {
        if (send(client_sockfd, send_buffer, bytes_read, 0) == -1) {
            syslog(LOG_ERR, "send failed: %s", strerror(errno));
            ret = RET_ERR;
            goto cleanup;
        }
        bytes_read = fread(send_buffer, 1, CHUNK_SIZE_BYTES, fp);
    }

    if (ferror(fp)) {
        syslog(LOG_ERR, "fread failed");
        ret = RET_ERR;
        goto cleanup;
    }

cleanup:
    if (0 != fclose(fp)) {
        syslog(LOG_ERR, "fclose failed: %s", strerror(errno));
        ret = RET_ERR;
    }
    return ret;
}
