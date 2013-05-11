/*
 * driver_dummy.cpp
 *
 *  Created on: May 1, 2013
 *      Author: rjlam
 */
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "replicamgr.h"
#include "msgs.h"

#define MAX_VERSION 10
#define RW_SIZE 131072  // 32 pages  

int connect(const char *name){
    struct sockaddr_in serv_addr;
    int conn;

    struct addrinfo *host;

    try {
        if (getaddrinfo(name, std::to_string((long long unsigned int)FBS_PORT).c_str(), NULL, &host) < 0){
            throw -1;
        }
        conn = socket(AF_INET, SOCK_STREAM, 0);
        if (conn < 0){
            perror("driver_dummy: Socket fail");
            throw conn;
        }

        if (connect(conn, host->ai_addr, host->ai_addrlen) < 0){
            perror("Error connecting");
            throw -1;
        }
    } catch (int e){
        conn = e;
    }

    freeaddrinfo(host);

    return conn;
}

int sendWriteRequest(int conn, uint32_t version, uint32_t off, char write_buf[], size_t len){
    struct fbs_header header;
    int bytesRW = 0;

    header.command = htons(FBS_WRITE);
    header.len = htonl(len);
    header.offset = htonl(off);
    header.req_num = htonl(version);
    header.seq_num = htonl(version);

    printf("Send Write Request: %d\n", conn);

    if ((bytesRW = send(conn, &header, sizeof(header), MSG_WAITALL)) < 0){
        perror("driver_dummy: send fail");
        return -1;
    }

    printf("Sent WRITE header\n");

    for(int i = 0; i < len-1; i++){
        write_buf[i] = random() % ('Z' - 'A' + 1) + 'A';
    }
    write_buf[len-1] = '\0';

    if ((bytesRW = send(conn, write_buf, len, MSG_WAITALL)) < 0){
        perror("driver_dummy: send fail");
        return -1;
    }

    printf("Sent WRITE buffer %d\n", len);

    return bytesRW;
}

int sendReadRequest(int conn, uint32_t version, uint32_t off, char read_buf[], size_t len){
    struct fbs_header header;
    struct fbs_response response;
    int bytesRW = 0;

    header.command = htons(FBS_READ);
    header.len = htonl(len);
    header.offset = htonl(off);
    header.req_num = htonl(version);
    header.seq_num = htonl(version);

    printf("Send Read Request: %d\n", conn);
    if ((bytesRW = send(conn, &header, sizeof(header), MSG_WAITALL))
            < 0) {
        perror("driver_dummy: send fail");
        return bytesRW;
    }

    printf("Sent READ header to %d\n", conn);

    // Receive
    if ((bytesRW = recv(conn, &response, sizeof(response),
            MSG_WAITALL)) < 0) {
        perror("driver_dummy: recv fail");
        return bytesRW;
    }

    response.status = ntohs(response.status);
    response.req_num = ntohl(response.req_num);

    printf("Received response %lu %lu\n", response.status, response.req_num);

    bytesRW = recv(conn, read_buf, len, MSG_WAITALL);
    if (bytesRW < 0) {
        perror("driver_dummy: recv fail");
        return bytesRW;
    }

    printf("Received %d data \n", bytesRW);

    return bytesRW;
}

int recvResponse(int conn){
    struct fbs_response response;
    int bytesRW;

    // Receive
    if ((bytesRW = recv(conn, &response, sizeof(response),
            MSG_WAITALL)) < 0) {
        perror("driver_dummy: recv fail");
        return bytesRW;
    }

    response.status = ntohs(response.status);
    response.req_num = ntohl(response.req_num);

    printf("Received response %lu %lu\n", response.status, response.req_num);

    return bytesRW;
}

/*
 * Tester functions
 * */

// Test propagation functionality
int testProp(int len, char *hostnames[]){
    struct fbs_response response;
    char write_buf[RW_SIZE];
    char read_buf[RW_SIZE];
    int bytesRW = 0;
    int accepted = 0;

    int *sock = new int[len];

    try {
        for (int i = 0; i < len; i++) {
            if ((sock[i] = connect(hostnames[i])) < 0) {
                throw sock[i];
            }
            printf("Connected to %d\n", sock[i]);
        }

        for (unsigned version = 1, off = 0; version <= MAX_VERSION; version++, off++) {
            sendWriteRequest(sock[0], version, off, write_buf,
                    sizeof(write_buf));

            accepted = 0;
            while (accepted < len) {
                printf("Receive %d\n", accepted);
                if ((bytesRW = recv(sock[accepted], &response,
                        sizeof(response), MSG_WAITALL)) < 0) {
                    perror("driver_dummy: recv fail");
                    throw bytesRW;
                }

                response.status = ntohs(response.status);
                response.req_num = ntohl(response.req_num);

                if (response.status == SUCCESS) {
                    accepted++;
                    printf("driver_dummy: write success\n");
                } else {
                    printf("driver_dummy: write fail\n");
                    throw -1;
                }
            }

            sendReadRequest(sock[len - 1], version, off, read_buf,
                    sizeof(read_buf));

            for (int x = 0; x < sizeof(write_buf); x++) {
                if (write_buf[x] != read_buf[x]) {
                    printf("First mismatch: %d\n", x);
                    throw -1;
                }
            }

        }
    } catch(int e){
        printf("testProp fail");
        bytesRW = e;
    }

    for (int i = 0; i < len; i++) {
        close(sock[i]);
    }

    delete [] sock;

    return bytesRW;
}

int testSync(int len, char *hostnames[]){
    struct fbs_response response;
    char write_buf[MAX_VERSION][RW_SIZE];
    char read_buf[MAX_VERSION][RW_SIZE];
    int bytesRW = 0;

    int *sock = new int[len];

    try {
        // Establish connection to primary
        if ((sock[0] = connect(hostnames[0])) < 0){
            throw sock[0];
        }

        // Write to primary
        for (unsigned version = 1, off = 0; version <= MAX_VERSION; version++, off++) {
            sendWriteRequest(sock[0], version, off, write_buf[off], sizeof(write_buf[off]));
            if ((bytesRW = recvResponse(sock[0])) < 0){
                throw bytesRW;
            }
        }

        sleep(10);  // Start up secondaries here

        // Establish connection to secondary
        for (unsigned i = 1; i < len; i++){
            if ((sock[i] = connect(hostnames[i])) < 0){
                throw sock[i];
            }
        }

        // Read from secondaries
        for (unsigned r = 1; r < len; r++){
            for (unsigned version = 1, off = 0; version <= MAX_VERSION; version++, off++) {
                sendReadRequest(sock[r], version, off, read_buf[off], sizeof(read_buf[off]));

                // Check for sameness
                for (unsigned i = 0; i < sizeof(write_buf[off]); i++){
                    if (write_buf[off][i] != read_buf[off][i]) {
                        printf("First mismatch @ Replica %d[%d]: %c %c\n", r,
                                i, write_buf[off][i], read_buf[off][i]);
                        throw -1;
                    }
                    // Clear read_buf
                    //read_buf[off][i] = 0;
                }
            }
        }

    } catch(int e){
        bytesRW = e;
    }

    delete [] sock;

    return bytesRW;

}

int main(int argc, char *argv[]){
    char **names = new char*[argc-1];
    int names_len = 0;

    int testNum = 0;

    if (argc < 2){
        printf("Not enough arguments\n");
        exit(1);
    }

    for (int i = 1; i < argc; i++){
        if (argv[i][0] != '-'){
            // Add to hosts
            names[names_len] = argv[i];
            names_len++;
            printf("Name: %x %s\n", argv[i], argv[i]);
            continue;
        }
        switch(argv[i][1]){
        case '1':
            testNum = 0;
            break;
        case '2':
            testNum = 1;
            break;
        default:
            printf("Invalid option\n");
            exit(1);
        }
    }

    switch(testNum){
    case 0:
        if (testProp(names_len, names) < 0){
            printf("Test prop failed\n");
            exit(1);
        }
        break;
    case 1:
        if (testSync(names_len, names) < 0){
            printf("Test sync failed\n");
            exit(1);
        }
        break;
    }

    return 0;
}
