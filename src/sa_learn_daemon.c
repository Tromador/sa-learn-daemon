#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>
#include <time.h>

#include "config.h"

#define BUF_SIZE 4096

static FILE *log_file = NULL;

/* --- Logging --- */
static void log_init(void) {
    if (strlen(LOG_FILE_PATH) > 0) {
        log_file = fopen(LOG_FILE_PATH, "a");
        if (!log_file) {
            perror("fopen log file");
            exit(EXIT_FAILURE);
        }
    } else {
        openlog("sa_learn_daemon", LOG_PID, LOG_FACILITY);
    }
}

static void log_close(void) {
    if (log_file) {
        fclose(log_file);
    } else {
        closelog();
    }
}

static void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (log_file) {
        vfprintf(log_file, fmt, ap);
        fputc('\n', log_file);
        fflush(log_file);
    } else {
        vsyslog(LOG_INFO, fmt, ap);
    }
    va_end(ap);
}

/* --- Signal handler --- */
static void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void fatal(const char *msg) {
    perror(msg);
    log_msg("Fatal error: %s (%s)", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

/* --- Daemonise --- */
static void daemonise(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }

    // Become session leader
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    // Fork again to ensure no controlling terminal
    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // Change working directory
    chdir("/");

    // Close stdio
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdio to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
}

int main(void) {
    int server_fd;
    struct sockaddr_un addr;

    log_init();
    log_msg("Starting sa_learn_daemon");

    // Daemonise early
    daemonise();

    // SIGCHLD handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        fatal("socket");

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(SOCKET_PATH);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        fatal("bind");

    if (listen(server_fd, 5) == -1)
        fatal("listen");

    log_msg("Listening on %s", SOCKET_PATH);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR)
                continue;
            fatal("accept");
        }

        pid_t pid = fork();
        if (pid < 0) {
            log_msg("fork failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            // Child process
            close(server_fd);

            // Read header line
            char header[BUF_SIZE];
            ssize_t pos = 0;
            char c;
            while (pos < (ssize_t)(sizeof(header) - 1)) {
                ssize_t r = read(client_fd, &c, 1);
                if (r <= 0) break;
                if (c == '\n') break;
                header[pos++] = c;
            }
            header[pos] = '\0';

            char *mode = strtok(header, " \t");
            char *user = strtok(NULL, " \t");
            if (!mode || !user) {
                log_msg("Invalid header line");
                close(client_fd);
                _exit(1);
            }

            int is_spam = 0;
            if (strcasecmp(mode, "SPAM") == 0) {
                is_spam = 1;
            } else if (strcasecmp(mode, "HAM") == 0) {
                is_spam = 0;
            } else {
                log_msg("Invalid mode: %s", mode);
                close(client_fd);
                _exit(1);
            }

            log_msg("Connection: mode=%s user=%s", mode, user);

            // Redirect socket to stdin
            if (dup2(client_fd, STDIN_FILENO) == -1) {
                log_msg("dup2 failed: %s", strerror(errno));
                close(client_fd);
                _exit(1);
            }
            close(client_fd);

            const char *mode_arg = is_spam ? "--spam" : "--ham";
            char *argv[] = {
                (char *)SA_LEARN_PATH,
                (char *)mode_arg,
                "-u",
                user,
                "-",
                NULL
            };

            execvp(SA_LEARN_PATH, argv);
            log_msg("execvp failed: %s", strerror(errno));
            _exit(1);
        }

        // Parent
        close(client_fd);
    }

    close(server_fd);
    log_close();
    return 0;
}
