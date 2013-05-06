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
#include "connmgr.h"


// Prototypes
void *handleConnection(void *arg);

void handleDriverConnection(enum conn_type src_type);
void handleReplicaConnection(enum conn_type src_type);

int handleReadRequest(enum conn_type src_type, struct fbs_request &request);
int handleWriteRequest(enum conn_type src_type, struct fbs_request &request);
int handleSyncRequest(enum conn_type src_type, uint64_t seq_num);
int handlePropRequest(enum conn_type src_type);

void handleExit(int sig);

// Global Variables
volatile sig_atomic_t eflag = 0;
ReplicaManager *rmgr;
ConnectionManager cmgr;

int main(int argc, char *argv[]){
    int status = 0;
    struct sigaction act;
    pthread_t conn_thread, d_thread, p_thread, n_thread;
    enum conn_type d = CONN_DRIVER, p = CONN_PREV, n = CONN_NEXT;

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
                    cmgr.update(CONN_PREV, prev);
                    cmgr.connect(CONN_PREV);
                    i++;
                }
                break;
            case 'n':
                if (i+1 < argc && next == NULL){
                    printf("Next name : %s\n");
                    next = argv[i+1];
                    cmgr.update(CONN_NEXT, next);
                    i++;
                }
                break;
            default:
                printf("Invalid option \n");
                exit(1);
        }
    }

    rmgr = new ReplicaManager();

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

    // Sync
   
    pthread_create(&d_thread, NULL, handleConnection, &d);
    pthread_create(&p_thread, NULL, handleConnection, &p);
    pthread_create(&n_thread, NULL, handleConnection, &n);

    pthread_join(d_thread, NULL);
    pthread_join(p_thread, NULL);
    pthread_join(n_thread, NULL);

    printf("Deleting rmgr\n");

    delete rmgr;    // Wait for all threads to finish then delete rmgr

    printf("Exiting main\n");
    return 0;
}

void *handleConnection(void *arg){
    struct pollfd fds[CONN_TYPE_LEN];
    enum conn_type sel = *((enum conn_type *)arg);

    while(!eflag){
        // Accept connection
        while(cmgr.poll_srv(fds) <= 0){ 
	        if (eflag) {
                pthread_exit(0); 
	        }
	    }
        if (fds[sel].revents & POLLIN){
            printf("Accepted connection from %d, fd=%d\n", sel, fds[sel].fd);
            cmgr.accept(sel);
        } else {
            continue;
        }
        switch(sel){
        case CONN_DRIVER:
            handleDriverConnection(sel);
            break;
        case CONN_NEXT:
            while(cmgr.connect(sel) < 0);
        case CONN_PREV:
            printf("Established connection to %d\n", sel);
            handleReplicaConnection(sel);
            break;
        }
    }
    pthread_exit(0);
}

/*
 * Driver side functions
 * */

// conn: fd for connection
void handleDriverConnection(enum conn_type src_type){
    struct fbs_header buffer;    // Only for header
    struct fbs_request req;
    int off, bytesRead = 0;
    int status = 0;
    struct fbs_header header;
    struct pollfd fds[CONN_TYPE_LEN];

    while (!eflag) {
//        printf("Thread: %d\n", src_type);
        while((status = cmgr.poll_conn(fds)) <= 0){
            if (eflag || status < 0){
                pthread_exit(0);
            }
        }
        if (fds[src_type].revents & (POLLHUP | POLLERR | POLLNVAL)){
            cmgr.close(src_type);
            return;
        }
        if (!(fds[src_type].revents & POLLIN)){
            continue;
        }
        if ((bytesRead = cmgr.recv_fr_cli(src_type, (char *)&buffer, sizeof(buffer))) <= 0){
            if (bytesRead == 0){
                printf("Socket closed by remote connection");
            } else {
                perror("ERROR reading from socket");
            }
            cmgr.close(src_type);
	        return;
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
                status = handleReadRequest(src_type, req);
                break;
            case FBS_WRITE:
                status = handleWriteRequest(src_type, req);
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
    printf("Exiting handleDriverConnection\n");
#endif
    cmgr.close(src_type);
    return;
}

// Sends response to driver, always.
int sendResponse(struct resp_data response, bool write) {
    int bytesWritten = 0;
    int rv;

#ifdef DEBUG
    printf("SendResponse: %u %u\n", ntohs(response.header.status), ntohl(response.header.req_num));
#endif

    if (( bytesWritten = cmgr.send_to_cli(CONN_DRIVER, (char *)&response.header, sizeof(response.header)) < 0)){
        return bytesWritten;
    }
    if(!write){
        bytesWritten = cmgr.send_to_cli(CONN_DRIVER, response.data, response.numBytes);
    }

#ifdef DEBUG
    printf("Wrote %d bytes\n", bytesWritten);
#endif
    return bytesWritten;
}

int handleReadRequest(enum conn_type src_type, struct fbs_request &request){
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
    
    status = sendResponse(response, false);

    delete [] response.data;

    return status;
}

// src_type : specify which client we are recieving data from. Should be 
// CONN_PREV or CONN_DRIVER
int handleWriteRequest(enum conn_type src_type, struct fbs_request &request){
    int bytesRW = 0;
    struct resp_data response;
    struct rmgr_sync_request prop_request;
    struct fbs_header prop_fbs_hdr;
    int dst;
        
    char *buffer;

    response.header.status = htons(SUCCESS);
    response.header.req_num = htonl(request.req_num);

    buffer = new char[request.len];

try{
#ifdef DEBUG
    printf("Server: Write to offset %u\n", request.offset * FBS_SECTORSIZE);
#endif
    prop_request.command = htons(RMGR_PROP);
    prop_request.seq_num = htonl(request.seq_num);

    prop_fbs_hdr.command = htons(request.command);
    prop_fbs_hdr.offset = htonl(request.offset);
    prop_fbs_hdr.len = htonl(request.len);
    prop_fbs_hdr.req_num = htonl(request.req_num);
    prop_fbs_hdr.seq_num = htonl(request.seq_num);

    cmgr.send_to_srv(CONN_NEXT, (char *)&prop_request, sizeof(prop_request));
    cmgr.send_to_srv(CONN_NEXT, (char *)&prop_fbs_hdr, sizeof(prop_fbs_hdr));

    if ((bytesRW = cmgr.recv_fr_cli(src_type, buffer, request.len)) < 0){
        throw bytesRW;
    }
#ifdef DEBUG
    printf("Server: recieved %d bytes, total: %lu\n", bytesRW, request.len);
#endif
    cmgr.send_to_srv(CONN_NEXT, buffer, request.len);

    if ((bytesRW = rmgr->write(request.offset, request.len / FBS_SECTORSIZE, 
                    request.seq_num, buffer) < 0)){
        throw bytesRW;
    }
} catch(int e){
	perror("ERROR handlewrite");
	delete [] buffer;
        return e;
}

    delete [] buffer;

    return sendResponse(response, true);
}




/*
 * Replica/controller functions
 * */
void handleReplicaConnection(enum conn_type src_type){
    int offset, bytesRead;
    int status = 0;
    struct rmgr_sync_request buffer, req;
    struct pollfd fds[CONN_TYPE_LEN];

    while(!eflag){
//        printf("Thread: %d\n", src_type);
        while((status = cmgr.poll_conn(fds)) <= 0){
            if (eflag || status < 0){
                pthread_exit(0);
            }
        }
        if (fds[src_type].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            cmgr.close(src_type);
            return;
        }
        if (!(fds[src_type].revents & POLLIN)) {
            continue;
        }
        if ((bytesRead = cmgr.recv_fr_cli(src_type, (char *) &buffer, sizeof(buffer))) <= 0) {
            if (bytesRead == 0){
                printf("Socket closed by remote connection");
            } else {
                perror("ERROR reading from socket");
            }
            cmgr.close(src_type);
            return;
        }

        // Switch endianness
        req.command = ntohs(buffer.command);
        req.seq_num = ntohl(buffer.seq_num);
#ifdef DEBUG    
        printf("Server req: %u %lu\n", req.command, req.seq_num);
#endif
        switch (req.command){
            case RMGR_SYNC:
                status = handleSyncRequest(src_type, req.seq_num);
                break;
            case RMGR_PROP:
                status = handlePropRequest(src_type);
		break;
            default:
                status = -1;
		break;
        }

        if (status < 0){
            perror("ERROR handling request");
        }
    }

#ifdef DEBUG
    printf("Exiting handleConnection\n");
#endif
    cmgr.close(src_type);
    return;
}

// Serve sync request by sending back writes
int handleSyncRequest(enum conn_type src_type, uint64_t seq_num){
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

    // Send header to client that requested SYNC
    if((bytesWritten = cmgr.send_to_cli(CONN_NEXT, (char *)&resp, sizeof(resp))) < 0){
        perror("ERROR sync send hdr fail");
        return bytesWritten;
    }
    if((bytesWritten = cmgr.send_to_cli(CONN_NEXT, (char *)&write_buf, write_len)) < 0){
        perror("Sync send fail");
        return bytesWritten;
    }

    if (write_buf != NULL) {
        free(write_buf); // Free that guy!
    }

    return bytesWritten;
}

int handlePropRequest(enum conn_type type){
    struct fbs_header buffer;   // 18 bytes?
    struct fbs_request req;
    int off = 0, bytesRead = 0;

    // Grab FBS header
    if ((bytesRead = cmgr.recv_fr_cli(CONN_PREV, (char *)&buffer, 
            sizeof(buffer))) < 0){
	    return bytesRead;
    }
   
    req.command = ntohs(buffer.command);
    req.len = ntohl(buffer.len);
    req.offset = ntohl(buffer.offset);
    req.seq_num = ntohl(buffer.seq_num);
    req.req_num = ntohl(buffer.req_num);

#ifdef DEBUG    
    printf("Server req: %u %lu %lu %lu %lu\n", req.command, req.len,
       req.offset, req.seq_num, req.req_num);
#endif

    handleWriteRequest(type, req);

}

void handleExit(int sig){
    if (sig & (SIGINT | SIGTERM | SIGQUIT | SIGABRT)){
        printf("Exit signal received\n");
        eflag = 1;
        //exit(0); // Figure out how to get this to do lsvd_close....?
    }
}

