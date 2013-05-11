#ifndef CONN_H
#define CONN_H

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netdb.h>

enum conn_type {
    CONN_DRIVER = 0,
    CONN_NEXT,
    CONN_PREV,
    CONN_TYPE_LEN
};

class ConnectionManager {
    struct pollfd serv_set[CONN_TYPE_LEN];      // Should only ever be modified on const/dest
    struct pollfd conn_set[CONN_TYPE_LEN];      // Set of fds for established connections on server socks

    int cli_psock;
    int cli_nsock;

    struct sockaddr_in p_addr;
    struct sockaddr_in n_addr;

    // Locks
    pthread_mutex_t p_lock;       // For client connection
    pthread_mutex_t n_lock;       // For client connection

public:
    ConnectionManager();
    ~ConnectionManager();

    int accept(enum conn_type sel);
    int connect(enum conn_type sel);
    int update(enum conn_type sel, const char *hostname);
    int poll_conn(struct pollfd *fds);
    int poll_srv(struct pollfd *fds);
    int send_to_cli(enum conn_type sel, char * buf, size_t len);    // TODO: A bit redundant, we can probably change this
    int recv_fr_cli(enum conn_type sel, char * buf, size_t len);
    int send_to_srv(enum conn_type sel, char * buf, size_t len);
    int recv_fr_srv(enum conn_type sel, char * buf, size_t len);
    int fwd_cli_stream(enum conn_type src, enum conn_type dst, char *buf, size_t len);
    int close(enum conn_type sel);

private:
    ConnectionManager(ConnectionManager&);
    int setup_srv(uint16_t port);
};

#endif
