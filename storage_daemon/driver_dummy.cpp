/*
 * driver_dummy.cpp
 *
 *  Created on: May 1, 2013
 *      Author: rjlam
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include "replicamgr.h"
#include "msgs.h"

// Scenario:
// We have driver, multiple sdaemons
// Driver tasks:
// Write a random array of data
// Wait for SUCCESS responses (should be 2+)

// Replica 1,2 tasks:
// Write data, propagate write, respond to driver
// Replica 3 tasks:
// Write data, respond to driver
#define QUORUM 2

int main(int argc, char **argv){
    struct fbs_header header;
    struct fbs_response response;
    struct sockaddr_in serv_addr;
    struct hostent *he;
    char write_buf[KERNEL_SECTOR_SIZE];
    char read_buf[KERNEL_SECTOR_SIZE];
    int bytesRW;
    int sock[QUORUM];
    int accepted = 0;

    for (int i = 0; i < argc - 1; i++){
        he = gethostbyname(argv[i+1]);

        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, he->h_addr, he->h_length);
        serv_addr.sin_port = htons(FBS_PORT);

        sock[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sock[i] < 0){
            perror("driver_dummy: Socket fail");
            exit(1);
        }


        if (connect(sock[i], (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
            perror("Error connecting");
            exit(1);
        }
    }

    for(unsigned version = 1, off = 0; version < 11; version++, off++){
        header.command = htons(FBS_WRITE);
        header.len = htonl(sizeof(write_buf));
        header.offset = htonl(off);
        header.req_num = htonl(version);
        header.seq_num = htonl(version);

        if ((bytesRW = send(sock[0], &header, sizeof(header), MSG_WAITALL)) < 0){
            perror("driver_dummy: send fail");
            exit(1);
        }

        printf("Sent WRITE header\n");

        for(int i = 0; i < sizeof(write_buf)-1; i++){
            write_buf[i] = random() % ('Z' - 'A' + 1) + 'A';
        }
        write_buf[sizeof(write_buf)] = '\0';

        if ((bytesRW = send(sock[0], &write_buf, sizeof(write_buf), MSG_WAITALL)) < 0){
            perror("driver_dummy: send fail");
            exit(1);
        }

        printf("Sent WRITE buffer\n");

	accepted = 0;
	while(accepted < argc-1){
            printf("Receive %d\n", accepted);
            if ((bytesRW = recv(sock[accepted], &response, sizeof(response), MSG_WAITALL)) < 0){
                perror("driver_dummy: recv fail");
                exit(1);
            }
            if (response.status == SUCCESS){
                accepted++;
                printf("driver_dummy: write success\n");
            } else {
                printf("driver_dummy: write fail\n");
                exit(1);
            }
	}
//******************* READ
        header.command = htons(FBS_READ);
        header.len = htonl(sizeof(read_buf));
        header.offset = htonl(off);
        header.req_num = htonl(version);
        header.seq_num = htonl(version);

        if ((bytesRW = send(sock[argc-2], &header, sizeof(header), MSG_WAITALL)) < 0){
            perror("driver_dummy: send fail");
            exit(1);
        }

        printf("Sent READ header %d\n", argc-2);

        // Receive
        if ((bytesRW = recv(sock[argc-2], &response, sizeof(response), MSG_WAITALL)) < 0){
            perror("driver_dummy: recv fail");
            exit(1);
        }

	printf("Received response %lu %lu\n", response.status, response.req_num);

        bytesRW = recv(sock[argc-2], &read_buf, sizeof(read_buf), MSG_WAITALL);
        if (bytesRW < 0){
	    perror("driver_dummy: recv fail");
	    exit(1);
        }
        
	printf("Received %d data %s\n", bytesRW, &read_buf);

        for(int x = 0; x < sizeof(write_buf); x++){
            if (write_buf[x] != read_buf[x]){
                printf("Mismatch: %d\n", x);
        	exit(1);
            }
        }

    }

    for (int i = 0; i < argc - 1; i++){
        close(sock[i]);
    }

    return 0;
}
