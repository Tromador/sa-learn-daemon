#ifndef CONFIG_H
#define CONFIG_H

/* Path to the sa-learn binary */
#define SA_LEARN_PATH "/usr/bin/sa-learn"

/* Path to the Unix domain socket */
#define SOCKET_PATH "/var/run/sa-learn.sock"

/* Socket permissions: will be set by systemd in practice */
#define SOCKET_MODE 0660

/* Optional log file: if empty string, use syslog */
#define LOG_FILE_PATH ""

/* Syslog facility: used if LOG_FILE_PATH is empty */
#define LOG_FACILITY LOG_MAIL

#endif /* CONFIG_H */
