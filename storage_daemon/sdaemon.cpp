/* 
 * Replica manager userspace process
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>

#include "replicamgr.h"

// Prototypes
void *handleDriver(void *arg);
void *handleReplica(void *arg);
void handleDriverConnection(int conn);
void handleReplicaConnection(int conn);
int handleReadRequest(int conn, struct fbs_request &request);
int handleWriteRequest(int conn, struct fbs_request &request);
int handleSyncRequest(int conn, uint64_t seq_num);
int handleUpdateRequest(int conn);
void handleExit(int sig);

// Global Variable
ReplicaManager *rmgr;

int main(int argc, char *argv[]){
    int status = 0;
    struct sigaction act;
    pthread_t driver_thread, replica_thread, controller_thread;

    act.sa_handler = handleExit;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGABRT, &act, NULL);

    rmgr = new ReplicaManager("localhost", "localhost", "localhost");

    if(argc != 3){
        printf("Usage: command [flags]\n");
        printf("-c PATH\tcreate lsvd_disk with pathname PATH\n");
        printf("-o PATH\topen existing lsvd_disk with pathname PATH\n");
        exit(1);
    }    
    
    switch(argv[1][1]){
        case 'c':
            printf("Path: %s\n", argv[2]);
            status = rmgr->create(argv[2], 1048576*FBS_SECTORSIZE/LSVD_SECTORSIZE);
            break;
        case 'o':
            printf("Path: %s\n", argv[2]);
            status = rmgr->open(argv[2]);
            break;
        default:
            printf("Invalid option \n");
            exit(1);
    }

    if (status < 0){
        perror("LSVD open error\n");
        exit(1);
    }
    
    rmgr->sync();   // Synchronize before responding

    pthread_create(&driver_thread, NULL, handleDriver, NULL);
#ifdef SYNC
    pthread_create(&replica_thread, NULL, handleReplica, NULL);
    pthread_create(&controller_thread, NULL, handleController, "localhost")
#endif
    pthread_join(driver_thread, NULL);
#ifdef SYNC
    pthread_join(replica_thread, NULL);
    pthread_join(controller_thread, NULL);
#endif
    delete rmgr;
    return 0;
}

void *handleDriver(void *arg){
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int setOpt = 1;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &setOpt, sizeof(setOpt));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(FBS_PORT);
    
    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on bind");
        exit(1);
    }

    listen(sockfd, 5);
    
    clilen = sizeof(cli_addr);
    while(1){
        if((newsockfd = accept(sockfd, 
                        (struct sockaddr *) &cli_addr, &clilen)) < 0){
            perror("ERROR accepting connection");
            continue;
        }
        handleDriverConnection(newsockfd); //
    }

    close(sockfd);
    pthread_exit(0);
}

// Handle sync requests from down stream
void *handleReplica(void *arg){
    int newsockfd;

    while(1){
        if((newsockfd = rmgr->accept_next()) < 0){
            perror("ERROR accepting connection");
            continue;
        }
        handleReplicaConnection(newsockfd);
    }
    pthread_exit(0);
}
#if 0
// handle updates from controller
void *handleController(void *arg){
    struct update_data buf;
    int bytesRead = 0;
    struct hostent *he;
    struct sinaddr_in ctrl_addr;
    socklen_t ctrl_len;
    int cSock;
    const char *ctrl = (const char *) arg;

    struct mgr_update_request buf;

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

    while(1){
        for(offset = 0; offset < ctrl_len; offset += bytesRead){
            bytesRead = recvfrom(cSock, ((char *)&buf)[offset], sizeof(buf) - offset, 0,
                    (struct sockaddr *) &ctrl_addr, &ctrl_len);
            if (bytesRead < 0){
                continue;
            }
        }
        // Handle message
        buf.prev.sin_addr = ntohl(buf.prev.sin_addr);
        buf.next.sin_addr = ntohl(buf.next.sin_addr);
        rmgr->update(buf.prev, buf.next);
    }
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
    int bytesRead = 0;
    int status = 0;
    struct fbs_header header;

    while(1){
        bytesRead = 0;
get_header:
        bytesRead += recv(conn, &buffer + bytesRead, sizeof(buffer) - bytesRead, 0); // Blocking
        if (bytesRead < 0){
            perror("ERROR reading from socket");
            close(conn);
            return;
        } else if (bytesRead < sizeof(header)){
            goto get_header;
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
    int status = 0;
    struct resp_data response;
        
    char *buffer;

    response.header.status = htons(SUCCESS);
    response.header.req_num = htonl(request.req_num);

    buffer = new char[request.len];
#ifdef DEBUG
    printf("Server: Write to offset %u\n", request.offset * FBS_SECTORSIZE);
#endif
    for (int req_offset = 0; req_offset < request.len;){
        status = recv(conn, &buffer[req_offset], request.len - req_offset, 0);
        if (status < 0){
            // TODO: do something about this?
            continue;
        }
        req_offset += status;
    }

    if ((status = rmgr->write(request.offset, request.len / FBS_SECTORSIZE, 
                    request.seq_num, buffer) < 0)){
        return status;
    }

    delete [] buffer;

    status = sendResponse(conn, response, true);

    return status;
}

/*
 * Replica/controller functions
 * */
void handleReplicaConnection(int conn){
    int bytesRead = 0;
    int status = 0;
    struct rmgr_sync_request buffer, req;

    while(1){
        bytesRead = recv(conn, &buffer, sizeof(buffer), 0); // Blocking
        if (bytesRead < 0){
            perror("ERROR reading from socket");
            close(conn);
            return;
        } else if (unsigned(bytesRead) < sizeof(buffer)){
            continue;
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
            // Just in case we want to add more functionality here...
            default:
                status = -1;
        }

        if (status < 0){
            perror("ERROR handling request");
        }

    }
}

// Serve sync request by sending back writes
int handleSyncRequest(int conn, uint64_t seq_num){
    uint64_t version = rmgr->get_local_version();
    struct rmgr_sync_response buff;
    int bytesWritten = 0;

    if (version > seq_num){
        // Get versions up to now and return it back
        printf("Call get_writes here\n");
    }

    buff.seq_num = htonl(0);    // End of data, or version too low
    buff.offset = 0;
    buff.length = 0;
    bytesWritten = send(conn, &buff, sizeof(buff), 0);

    return bytesWritten;
}

void handleExit(int sig){
    if (sig & (SIGINT | SIGTERM | SIGQUIT | SIGABRT)){
        printf("Exiting\n");
	if(rmgr != NULL){
            delete rmgr;
            rmgr = NULL;
            exit(1);
        }
    }
}
