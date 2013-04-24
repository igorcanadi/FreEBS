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

#include "replicamgr.h"
#define FBS_PORT            9000

struct resp_data{
    __be16 status;
    __be32 seq_num;
    char *data;
    unsigned numBytes;
};

// Prototypes
void handleConnection(int conn);
int handleReadRequest(int conn, struct fbs_request &request);
int handleWriteRequest(int conn, struct fbs_request &request);
    
ReplicaManager *rmgr;

int main(int argc, char *argv[]){
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int status = 0;

    rmgr = new ReplicaManager(1, 1, 1);

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
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("ERROR opening socket");
        exit(1);
    }

    int setOpt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &setOpt, sizeof(setOpt));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(FBS_PORT);
    
    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on bind");
        exit(1);
    }

    listen(sockfd, 5);
    
    // Set up client stuff
    clilen = sizeof(cli_addr);

    while(1){
        if((newsockfd = accept(sockfd, 
                        (struct sockaddr *) &cli_addr, &clilen)) < 0){
            perror("ERROR accepting connection");
            continue;
        }
        handleConnection(newsockfd); // One connection at a time
    }

    close(sockfd);

    return 0;
}

// conn: fd for connection
void handleConnection(int conn){
    struct fbs_header buffer;    // Only for header
    struct fbs_request req;
    int bytesRead = 0;
    int status = 0;
    struct fbs_header header;

    while(1){
        bytesRead = recv(conn, &buffer, sizeof(buffer), 0); // Blocking
        if (bytesRead < 0){
            perror("ERROR reading from socket");
            close(conn);
            return;
        } else if (unsigned(bytesRead) < sizeof(header)){
            continue;
        }
        
        // Switch endianness?
        req.command = ntohs(buffer.command);
        req.len = ntohl(buffer.len);
        req.offset = ntohl(buffer.offset);
        req.seq_num = ntohl(buffer.seq_num);
#ifdef DEBUG    
        printf("Server req: %u %u %u %u\n", req.command, req.len, 
                req.offset, req.seq_num);
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
    struct fbs_response sendHeader;

    sendHeader.status = response.status;
    sendHeader.seq_num = response.seq_num;
#ifdef DEBUG
    printf("SendResponse: %u %u\n", ntohs(sendHeader.status), ntohl(sendHeader.seq_num));
#endif
    bytesWritten = send(conn, &sendHeader.status, sizeof(sendHeader.status), 0);  // Write header out
    if (bytesWritten < 0){ 
        return bytesWritten;    
    }
    bytesWritten += send(conn, &sendHeader.seq_num, sizeof(sendHeader.seq_num), 0);
    if (bytesWritten < 0){
        return bytesWritten;
    }

    if(!write){
        bytesWritten += send(conn, response.data, response.numBytes, 0);
        if (bytesWritten < 0){
            return bytesWritten;
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
    response.status = htons(SUCCESS);
    response.seq_num = htonl(request.seq_num);

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

    response.status = htons(SUCCESS);
    response.seq_num = htonl(request.seq_num);

    buffer = new char[request.len];
#ifdef DEBUG
    printf("Server: Write to offset %u\n", request.offset * FBS_SECTORSIZE);
#endif
    for (int req_offset = 0; req_offset < request.len;){
        status = recv(conn, &buffer[req_offset], request.len - req_offset, 0);
        if (status < 0){
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

