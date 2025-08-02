#ifndef CONFIG_H
#define CONFIG_H

/* Path to the sa-learn binary */
#define SA_LEARN_PATH "/usr/bin/sa-learn"

/* Path to the Unix domain socket */
#define SOCKET_PATH "/var/run/sa-learn.sock"

/* Socket permissions: will be set by systemd in practice */
#define SOCKET_MODE 0660

#endif /* CONFIG_H */
