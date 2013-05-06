#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>   // C++ stringz

#include "connmgr.h"
#include "replicamgr.h"

ConnectionManager::ConnectionManager(){
    if ((serv_set[CONN_PREV].fd = setup_srv(PROP_PORT)) < 0){
        exit(1);
    }
    if ((serv_set[CONN_NEXT].fd = setup_srv(SYNC_PORT)) < 0){
        exit(1);
    }
    if ((serv_set[CONN_DRIVER].fd = setup_srv(FBS_PORT)) < 0){
        exit(1);
    }

    for(int i = 0; i < CONN_TYPE_LEN; i++){
        serv_set[i].events = POLLIN;
    }

    cli_psock = -1;
    cli_nsock = -1;

    p_addr.sin_family = AF_INET;
    p_addr.sin_port = htons(SYNC_PORT);
    n_addr.sin_family = AF_INET;
    n_addr.sin_port = htons(PROP_PORT);

    pthread_mutex_init(&p_lock, NULL);
    pthread_mutex_init(&n_lock, NULL);

}

ConnectionManager::~ConnectionManager(){
    for (unsigned i = 0; i < CONN_TYPE_LEN; i++){
        ::close(serv_set[i].fd);
    }

    ::close(cli_psock);
    ::close(cli_nsock);

    pthread_mutex_destroy(&p_lock);
    pthread_mutex_destroy(&n_lock);

}

// Private
int ConnectionManager::setup_srv(uint16_t port){
    int sockfd;
    int setOpt = 1;
    int flags;

    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        return sockfd;
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &setOpt, sizeof(setOpt));
/*
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
*/
    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on bind");
        return -1;
    }

    if (listen(sockfd, 5) < 0){
        perror("ERROR on listen");
        return -1;
    }

    printf("SOCKFD %d\n", sockfd);

    return sockfd;
}

// We accept act as server and accept connections
int ConnectionManager::accept(enum conn_type sel){
    int conn;
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    // Update fd_set with conn
    conn = ::accept(serv_set[sel].fd, (struct sockaddr *) &cli_addr, &clilen);
    conn_set[sel].fd = conn;
    conn_set[sel].events = POLLIN;    // Requested
    conn_set[sel].revents = 0;

    switch(sel){
    case CONN_NEXT:
        n_addr.sin_addr.s_addr = cli_addr.sin_addr.s_addr;
        break;
    case CONN_PREV:
        p_addr.sin_addr.s_addr = cli_addr.sin_addr.s_addr;
        break;
    default:
        break;
    }

    return conn;
}

// We connect as a client to remote server
int ConnectionManager::connect(enum conn_type sel){
    int *sock;
    pthread_mutex_t *lock;
    struct sockaddr_in *serv_addr;
    int status = 0;

    switch(sel){
    case CONN_PREV:
        sock = &cli_psock;
        lock = &p_lock;
        serv_addr = &p_addr;
        break;
    case CONN_NEXT:
        sock = &cli_nsock;
        lock = &n_lock;
        serv_addr = &n_addr;
        break;
    default:
        return -1;
    }

    // Connect to server
    pthread_mutex_lock(lock);
    if ((*sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        pthread_mutex_unlock(lock);
        return *sock;
    }
    if ((status = ::connect(*sock, (struct sockaddr *) serv_addr, sizeof(struct sockaddr_in))) < 0){
        ::close(*sock);
        *sock = -1;
    }
    pthread_mutex_unlock(lock);

    return status;
}


int ConnectionManager::update(enum conn_type sel, const char *host){
    int status;
    struct sockaddr_in *addr;
    pthread_mutex_t *lock;
    struct hostent *he = gethostbyname(host);
    long long unsigned port;

    struct addrinfo *res;
    std::string port_str;      // Max port char len + null

    switch(sel){
    case CONN_PREV:
        addr = &p_addr;
        lock = &p_lock;
        port = SYNC_PORT;
        break;
    case CONN_NEXT:
        addr = &n_addr;
        lock = &n_lock;
        port = PROP_PORT;
	break;
    default:
	return -1;
    }
    
    pthread_mutex_lock(lock);
    port_str = std::to_string(port);
    status = getaddrinfo(host, port_str.c_str(), NULL, &res);
    memcpy(addr, res[0].ai_addr, res[0].ai_addrlen);
    pthread_mutex_unlock(lock);
    freeaddrinfo(res);

    return 0;
}

// Wait for receipt of activity on established connections where we are the "server"
int ConnectionManager::poll_conn(struct pollfd *fds){
    int status;

    status = poll(&conn_set[0], sizeof(conn_set)/sizeof(struct pollfd), 2);
    memcpy(fds, &conn_set[0], sizeof(conn_set));

    return status;
}

int ConnectionManager::poll_srv(struct pollfd *fds){
    int status;
    status = poll(&serv_set[0], sizeof(serv_set)/sizeof(struct pollfd), 2);
    memcpy(fds, &serv_set[0], sizeof(serv_set));
    return status;
}

// Send something to the client process
int ConnectionManager::send_to_cli(enum conn_type sel, char * buf, size_t len){
    int off, bytes, fd;

    fd = conn_set[sel].fd;

    for (off = 0, bytes = 0; off < len; off += bytes){
        bytes = send(fd, buf + off, len - off, 0);
        if (bytes < 0){
            return bytes;
        }
    }
#ifdef DEBUG
    printf("send_to_cli: %d bytes\n", off);
#endif
    return off;
}

// Recv from a client
int ConnectionManager::recv_fr_cli(enum conn_type sel, char * buf, size_t len){
    int off, bytes, fd;

    fd = conn_set[sel].fd;

    for (off = 0, bytes = 0; off < len; off += bytes){
        bytes = recv(fd, buf + off, len - off, 0);
        if (bytes <= 0){
            return bytes;
        }
    }
#ifdef DEBUG
    printf("recv_fr_cli: %d bytes\n", off);
#endif
    return off;
}

// Send something to the server
int ConnectionManager::send_to_srv(enum conn_type sel, char * buf, size_t len){
    int off, bytes;
    int fd;
    switch(sel){
    case CONN_PREV:
        pthread_mutex_lock(&p_lock);
        fd = cli_psock;
        pthread_mutex_unlock(&p_lock);
        break;
    case CONN_NEXT:
        pthread_mutex_lock(&n_lock);
        fd = cli_nsock;
        pthread_mutex_unlock(&n_lock);
        break;
    default:
        return -1;  // Invalid
    }

    for (off = 0, bytes = 0; off < len; off += bytes){
        bytes = send(fd, buf + off, len - off, 0);
        if (bytes < 0){
            return bytes;
        }
    }
#ifdef DEBUG
    printf("send_to_srv: %d bytes\n", off);
#endif
    return off;
}

// Recv from the server
int ConnectionManager::recv_fr_srv(enum conn_type sel, char * buf, size_t len){
    int off, bytes;
    int fd;
    switch(sel){
    case CONN_PREV:
        pthread_mutex_lock(&p_lock);
        fd = cli_psock;
        pthread_mutex_unlock(&p_lock);
        break;
    case CONN_NEXT:
        pthread_mutex_lock(&n_lock);
        fd = cli_nsock;
        pthread_mutex_unlock(&n_lock);
        break;
    default:
        return -1;  // Invalid
    }

    for (off = 0, bytes = 0; off < len; off += bytes){
        bytes = recv(fd, buf + off, len - off, 0);
        if (bytes <= 0){
            return bytes;
        }
    }

    return off;
}

// Close a connection for which we are a client
int ConnectionManager::close(enum conn_type sel){
    switch(sel){
    case CONN_PREV:
        pthread_mutex_lock(&p_lock);
        ::close(cli_psock);
        cli_psock = -1;
        pthread_mutex_unlock(&p_lock);
        break;
    case CONN_NEXT:
        pthread_mutex_lock(&n_lock);
        ::close(cli_nsock);
        cli_nsock = -1;
        pthread_mutex_unlock(&n_lock);
        break;
    default:
        return -1;  // Invalid
    }
    return 0;
}

