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

#include "lsvd.h"
#include "freebs.h"

    
#define FBS_SECTORSIZE      KERNEL_SECTOR_SIZE
#define LSVD_SECTOR_SIZE    SECTOR_SIZE
#define FBS_PORT            9000


struct resp_data{
    __be16 status;
    __be32 seq_num;
    char *data;
    unsigned numBytes;
};

// Prototypes
void handleConnection(int conn);
int handleReadRequest(int conn, struct fbs_header &request);
int handleWriteRequest(int conn, struct fbs_header &request);
    
char *volume;
int main(){
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    // Initialize volume
    volume = (char *) calloc(sizeof(char), (2048*FBS_SECTORSIZE)*(2048*FBS_SECTORSIZE));

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

    free(volume);
    close(sockfd);

    return 0;
}

// conn: fd for connection
void handleConnection(int conn){
    struct fbs_header buffer;    // Only for header
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
        header.command = ntohs(buffer.command);
        header.len = ntohs(buffer.len);
        header.offset = ntohs(buffer.offset);
        header.seq_num = ntohs(buffer.seq_num);
    
        printf("Server Header: %x %x %x %x\n", header.command, header.len, header.offset, header.seq_num);

        switch (header.command){
            case FBS_READ:
                printf("Read\n");
                status = handleReadRequest(conn, header);
                break;
            case FBS_WRITE:
                printf("Write\n");
                status = handleWriteRequest(conn, header);
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

    sendHeader.status = htons(response.status);
    sendHeader.seq_num = htons(response.seq_num);

    printf("SendResponse: %X %X", sendHeader.status, sendHeader.seq_num);

try{
    bytesWritten = send(conn, &sendHeader.status, sizeof(sendHeader.status), 0);  // Write header out
    if (bytesWritten < 0){ 
        throw bytesWritten;    
    }
    bytesWritten = send(conn, &sendHeader.seq_num, sizeof(sendHeader.seq_num), 0);
    if (bytesWritten < 0){
        throw bytesWritten;
    }

    if(!write){
        bytesWritten = send(conn, response.data, response.numBytes, 0);
        if (bytesWritten < 0){
            throw bytesWritten;
        }
    }
} catch(int e){
    if (!write){
        delete [] response.data;
    }
    perror("Errororor");
} 
    return bytesWritten;

}

int handleReadRequest(int conn, struct fbs_header &request){
    int status = 0;
    struct resp_data response;
    int min, max;

    printf("Read from %X", request.offset);

    response.status = SUCCESS;
    response.seq_num = request.seq_num;

    min = FBS_SECTORSIZE * request.offset;  // Byteoffset
    max = min + request.len;                // Byteoffset

    response.data = new char[max - min + 1]; // Allocate space
    memcpy((void *) response.data, (void *) &volume[min], max - min + 1);   // Read volume data into data
    
    status = sendResponse(conn, response, false);

    return status;
}

int handleWriteRequest(int conn, struct fbs_header &request){
    int status = 0;
    struct resp_data response;
        
    response.status = SUCCESS;
    response.seq_num = request.seq_num;

    // Read length * FBS_SECTORSIZE bytes from connection
    for (int req_offset = 0, vol_offset = request.offset*FBS_SECTORSIZE;
            req_offset < request.len;){
        status = recv(conn, &volume[vol_offset], request.len - req_offset, 0);
        printf("Server: Write to offset %X\n", vol_offset);
        if (status < 0){
            continue;
        }
        req_offset += status;
        vol_offset += status;
    }

    status = sendResponse(conn, response, true);

    return status;
}

