/* 
 * Replica manager userspace process
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>

#include "replicamgr.h"

enum conn_type {
    CONN_DRIVER = 0,
    CONN_NEXT,
    CONN_PREV
};

// Prototypes
void *handleConnection(void *arg);
void handleDriverConnection(int conn);
void handleReplicaConnection(int conn);
int handleReadRequest(int conn, struct fbs_request &request);
int handleWriteRequest(int conn, struct fbs_request &request);
int handleSyncRequest(int conn, uint64_t seq_num);
int handlePropRequest(int conn);
int handleUpdateRequest(int conn);
void handleExit(int sig);

// Global Variables
ReplicaManager *rmgr;
int driver_conn = -1;
volatile sig_atomic_t eflag = 0;
pthread_mutex_t dLock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]){
    int status = 0;
    struct sigaction act;
    pthread_t driver_thread, prev_thread, next_thread, controller_thread;
    enum conn_type t1 = CONN_DRIVER, t2 = CONN_PREV, t3 = CONN_NEXT;

    bool create;
    char *path = NULL, *next = NULL, *prev = NULL;

    int sig;
    act.sa_handler = handleExit;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGINT);
    sigaddset(&act.sa_mask, SIGTERM);
    sigaddset(&act.sa_mask, SIGQUIT);
    sigaddset(&act.sa_mask, SIGABRT);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGABRT, &act, NULL);

    if(argc < 3){
        printf("Usage: command [flags]\n");
        printf("-c PATH\tcreate lsvd_disk with pathname PATH\n");
        printf("-o PATH\topen existing lsvd_disk with pathname PATH\n");
        printf("-p NAME\tconnect replica to upstream host with NAME");
        printf("-n NAME\tconnect replica to downstream host with NAME");
        exit(1);
    }    
    
    for (int i = 1; i < argc; i++){
        switch(argv[i][1]){
            case 'c':
                if (i+1 < argc && path == NULL){
                    create = true;
                    printf("Path: %s\n", argv[i+1]);
                    path = argv[i+1];
                    i++;
                }
                break;
            case 'o':
                if (i+1 < argc && path == NULL){
                    create = false;
                    printf("Path: %s\n", argv[i+1]);
                    path = argv[i+1];
                    i++;
                }
                break;
            case 'p':
                if (i+1 < argc && prev == NULL){
                    printf("Previous name : %s\n", argv[i+1]);
                    prev = argv[i+1];
                    i++;
                }
                break;
            case 'n':
                if (i+1 < argc && next == NULL){
                    printf("Next name : %s\n");
                    next = argv[i+1];
                    i++;
                }
                break;
            default:
                printf("Invalid option \n");
                exit(1);
        }
    }

    rmgr = new ReplicaManager(prev, next);
    if (path != NULL){
        if (create){
            status = rmgr->create(path, 1048576*FBS_SECTORSIZE/LSVD_SECTORSIZE);
        } else {
            status = rmgr->open(path);
        }
    } else {
        printf("Invalid input parameters\n");
        exit(1);
    }

    if (status < 0){
        perror("LSVD open error\n");
        exit(1);
    }
#ifdef SYNC
    if (prev != NULL){
//        rmgr->sync();   // Synchronize before responding
    }
#endif
    pthread_create(&driver_thread, NULL, handleConnection, &t1);
#ifdef SYNC
    pthread_create(&prev_thread, NULL, handleConnection, &t2);
    pthread_create(&next_thread, NULL, handleConnection, &t3);
#endif
    sigwait(&act.sa_mask, &sig);
    pthread_cancel(driver_thread);
#ifdef SYNC
    pthread_cancel(prev_thread);
    pthread_cancel(next_thread);
#endif
    printf("Exiting main\n");
    delete rmgr;    // Wait for all threads to finish then delete rmgr
    return 0;
}

// Sets up server socket and binds socket to port
int setup_server(struct sockaddr_in &serv_addr){
    int sockfd;
    int setOpt = 1;

    int flags;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &setOpt, sizeof(setOpt));
/*
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
*/
    printf("SOCKFD %d\n", sockfd);

    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on bind");
        exit(1);
    }

    if (listen(sockfd, 5) < 0){
        perror("ERROR on listen");
        exit(1);
    }
    return sockfd;
}

void *handleConnection(void *arg){
    int sockfd, newsockfd;
    int tmp;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    enum conn_type type = *((enum conn_type *)arg);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    switch(type){
    case CONN_DRIVER:
        serv_addr.sin_port = htons(FBS_PORT);
        break;
    case CONN_PREV:
        serv_addr.sin_port = htons(PROP_PORT);
        break;
    case CONN_NEXT:
        serv_addr.sin_port = htons(SYNC_PORT);
        break;
    }
    sockfd = setup_server(serv_addr);
    
    clilen = sizeof(cli_addr);
    while(!eflag){
        newsockfd = accept(sockfd,(struct sockaddr *) &cli_addr, &clilen);
        switch(type){
        case CONN_DRIVER:
            pthread_mutex_lock(&dLock);  // SHOULD BE ONLY PLACE dLock is modified!!
            driver_conn = newsockfd;
            pthread_mutex_unlock(&dLock);
            if (newsockfd < 0){
                perror("ERROR accepting connection");
                continue;
            }
            printf("Driver connection accepted \n");
            handleDriverConnection(newsockfd);
            break;
        case CONN_PREV:
        case CONN_NEXT:
            if (newsockfd < 0) {
                perror("ERROR accepting connection");
                continue;
            }
            if (type == CONN_NEXT) {
                printf("Accepted conn from next\n");
                rmgr->update(NULL, &cli_addr.sin_addr);
                while (tmp = rmgr->conn_next() < 0);
                printf("Connection to next established %d\n", tmp);
            } else {
                printf("Accepted conn to prev\n");
            }
            handleReplicaConnection(newsockfd);
            break;
        default:
            printf("Incorrect type\n");
            break;
        }
    }

    close(sockfd);
    pthread_exit(0);
}

#if ENABLE_CTRL
// handle updates from controller
void *handleController(void *arg){
    struct update_data buf;
    int bytesRead = 0;
    struct hostent *he;
    struct sinaddr_in ctrl_addr;
    socklen_t ctrl_len;
    int cSock;
    const char *ctrl = (const char *) arg;

    struct ctrl_update_request buf;

    if ((cSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    he = gethostbyname(ctrl);
    ctrl_addr.sin_family = AF_INET;
    memcpy(&ctrl_addr.sin_addr.s_addr, he->h_addr_list, he->h_length);
    ctrl_addr.sin_port=htons(CTRL_PORT);
    ctrl_len = sizeof(ctrl_addr);

    if(bind(cSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on bind");
        exit(1);
    }

    while(!eflag){
        for(offset = 0; offset < sizeof(buf); offset += bytesRead){
            bytesRead = recvfrom(cSock, &buf + offset, sizeof(buf) - offset, 0,
                    (struct sockaddr *) &ctrl_addr, &ctrl_len);
            if (bytesRead < 0){
                continue;
            }
        }
        // Handle message
        buf.prev.sin_addr = ntohl(buf.prev.sin_addr);
        buf.next.sin_addr = ntohl(buf.next.sin_addr);
        rmgr->update(&buf.prev, &buf.next);
    }

    close(cSock);
    pthread_exit(0);
}
#endif
/*
 * Driver side functions
 * */

// conn: fd for connection
void handleDriverConnection(int conn){
    struct fbs_header buffer;    // Only for header
    struct fbs_request req;
    int off, bytesRead = 0;
    int status = 0;
    struct fbs_header header;

    while (!eflag) {
        for (off = 0, bytesRead = 0; off < sizeof(buffer) && !eflag; off
                += bytesRead) {
            bytesRead = recv(conn, &buffer + off, sizeof(buffer) - off, 0); // Blocking
            if (bytesRead < 0 || eflag) {
                perror("ERROR reading from socket");
                close(conn);
                return;
            }
        }
 
        // Switch endianness
        req.command = ntohs(buffer.command);
        req.len = ntohl(buffer.len);
        req.offset = ntohl(buffer.offset);
        req.seq_num = ntohl(buffer.seq_num);
        req.req_num = ntohl(buffer.req_num);
#ifdef DEBUG    
        printf("Server req: %u %u %u %u %u\n", req.command, req.len, 
                req.offset, req.seq_num, req.req_num);
#endif
        switch (req.command){
            case FBS_READ:
                status = handleReadRequest(conn, req);
                break;
            case FBS_WRITE:
                status = handleWriteRequest(conn, req);
                break;
            default:
                perror("ERROR Invalid command");
                continue;
        }

        if (status < 0){
            perror("ERROR handling request");
        }
    }
#ifdef DEBUG
    printf("Exiting handleConnection");
#endif
    close(conn);
    return;
}

// data
int sendResponse(int conn, struct resp_data response, bool write) {
    int bytesWritten = 0;
    int rv;

#ifdef DEBUG
    printf("SendResponse: %u %u\n", ntohs(response.header.status), ntohl(response.header.req_num));
#endif
    while (bytesWritten < sizeof(response.header)) {
        rv = send(conn, &response.header + bytesWritten, sizeof(response.header) - bytesWritten, 0);
        if (rv < 0)
            return rv;
        bytesWritten += rv;
    }

    if(!write){
        bytesWritten = 0;
        while (bytesWritten < sizeof(response.header)) {
            rv = send(conn, response.data, response.numBytes, 0);
            if (rv < 0)
                return rv;
            bytesWritten += rv;
        }
    }
#ifdef DEBUG
    printf("Wrote %d bytes\n", bytesWritten);
#endif
    return bytesWritten;
}

int handleReadRequest(int conn, struct fbs_request &request){
    int status = 0;
    struct resp_data response;
    int min, max;
#ifdef DEBUG
    printf("Read from %u\n", request.offset);
#endif
    response.header.status = htons(SUCCESS);
    response.header.req_num = htonl(request.req_num);

    min = FBS_SECTORSIZE * request.offset;  // Byteoffset
    max = min + request.len;                // Byteoffset

    response.data = new char[max - min + 1]; // Allocate space
    if ((status = rmgr->read(request.offset, request.len / FBS_SECTORSIZE, 
                    request.seq_num, response.data)) < 0){
        return status;
    }
    response.numBytes = max - min;
    
    status = sendResponse(conn, response, false);

    delete [] response.data;

    return status;
}

int handleWriteRequest(int conn, struct fbs_request &request){
    int bytesRW = 0;
    struct resp_data response;
    struct rmgr_sync_request prop_request;
    struct fbs_header prop_fbs_hdr;
    int dst;
        
    char *buffer;

    response.header.status = htons(SUCCESS);
    response.header.req_num = htonl(request.req_num);

    buffer = new char[request.len];
#ifdef DEBUG
    printf("Server: Write to offset %u, sectors %u\n", request.offset * FBS_SECTORSIZE, 
	request.len / FBS_SECTORSIZE);
#endif
    prop_request.command = htons(RMGR_PROP);
    prop_request.seq_num = htonl(request.seq_num);

    prop_fbs_hdr.command = htons(request.command);
    prop_fbs_hdr.offset = htonl(request.offset);
    prop_fbs_hdr.len = htonl(request.len);
    prop_fbs_hdr.req_num = htonl(request.req_num);
    prop_fbs_hdr.seq_num = htonl(request.seq_num);

    rmgr->send_next((char *)&prop_request, sizeof(rmgr_sync_request));
    rmgr->send_next((char *)&prop_fbs_hdr, sizeof(fbs_request));

    for (int req_offset = 0, bytesRW = 0; req_offset < request.len; req_offset += bytesRW){
        bytesRW = recv(conn, &buffer[req_offset], request.len - req_offset, 0);
        if (bytesRW < 0){
            perror("ERROR write recv");
            // TODO: do something about this?
            return bytesRW;
        }
        printf("Server: recieved %d bytes, total: %d\n", bytesRW, request.len);
        rmgr->send_next(&buffer[req_offset], bytesRW);
    }

    if ((bytesRW = rmgr->write(request.offset, request.len / FBS_SECTORSIZE, 
                    request.seq_num, buffer) < 0)){
        return bytesRW;
    }

    delete [] buffer;

    // Send response to driver
    pthread_mutex_lock(&dLock);
    dst = driver_conn;
    pthread_mutex_unlock(&dLock);
    bytesRW = sendResponse(dst, response, true);

    return bytesRW;
}




/*
 * Replica/controller functions
 * */
void handleReplicaConnection(int conn){
    int offset, bytesRead;
    int status = 0;
    struct rmgr_sync_request buffer, req;

    while(!eflag){
        for (offset = 0, bytesRead = 0; offset < sizeof(buffer); offset += bytesRead){
            bytesRead = recv(conn, &buffer + offset, sizeof(buffer) - offset, 0); 
            if (bytesRead < 0 || eflag){
                perror("ERROR reading from socket");
                close(conn);
                return;
            }
        }

        // Switch endianness
        req.command = ntohs(buffer.command);
        req.seq_num = ntohl(buffer.seq_num);
#ifdef DEBUG    
        printf("Server req: %u %lu\n", req.command, req.seq_num);
#endif
        switch (req.command){
            case RMGR_SYNC:
                status = handleSyncRequest(conn, req.seq_num);
                break;
            case RMGR_PROP:
                status = handlePropRequest(conn);
            default:
                status = -1;
        }

        if (status < 0){
            perror("ERROR handling request");
            close(conn);
            return;
        }

    }

#ifdef DEBUG
    printf("Exiting handleConnection");
#endif
    close(conn);
    return;
}

// Serve sync request by sending back writes
int handleSyncRequest(int conn, uint64_t seq_num){
    uint32_t version = rmgr->get_local_version();   // Casting.. change this later
    struct rmgr_sync_response resp;
    int bytesWritten = 0;

    char *write_buf = NULL;
    size_t write_len;

    if (version > seq_num){
        write_buf = rmgr->get_writes_since(seq_num, &write_len);
        resp.seq_num = htonl(seq_num);
    } else {    // Version too low, don't send writes
        resp.seq_num = 0;
        resp.size = 0;
    }

    // Send header
    bytesWritten = send(conn, &resp, sizeof(resp), 0);
    for(int offset = 0, bytesWritten = 0; offset < write_len; offset += bytesWritten){
        if ((bytesWritten = send(conn, &write_buf + offset, write_len - offset, 0)) < 0){
            perror("Sync send fail");
            break;
        }
    }

    if (write_buf != NULL) {
        free(write_buf); // Free that guy!
    }

    return bytesWritten;
}

int handlePropRequest(int conn){
    struct fbs_header buffer;
    struct fbs_request req;
    int off = 0, bytesRead = 0;

    for(off = 0, bytesRead = 0; off < sizeof(buffer); off += bytesRead){
        bytesRead = recv(conn, &buffer + off, sizeof(buffer) - off, 0);
        if (bytesRead < 0){
            return bytesRead;
        }
    }
    
    req.command = ntohs(buffer.command);
    req.len = ntohl(buffer.len);
    req.offset = ntohl(buffer.offset);
    req.seq_num = ntohl(buffer.seq_num);
    req.req_num = ntohl(buffer.req_num);

#ifdef DEBUG    
    printf("Server buf: %u %lu %lu %lu %lu\n", buffer.command, buffer.len,
       buffer.offset, buffer.seq_num, buffer.req_num);
    printf("Server req: %u %lu %lu %lu %lu\n", req.command, req.len,
       req.offset, req.seq_num, req.req_num);
#endif

    handleWriteRequest(conn, req);

}

void handleExit(int sig){
    if (sig & (SIGINT | SIGTERM | SIGQUIT | SIGABRT)){
        printf("Exit signal received\n");
        eflag = 1;
        exit(0); // Figure out how to get this to do lsvd_close....?
    }
}

