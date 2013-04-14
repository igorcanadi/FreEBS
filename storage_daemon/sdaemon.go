package main

/*
#include "msgs.h"
 */
import "C"
// C libraries above this point.

import (
    "os"
    "fmt"
    "net"
    "bytes"
    "encoding/binary"
)

// Constants
const FBS_BLOCKSIZE uint32 = 4096   // Bytes in a block
const FBS_SECTORSIZE = C.FBS_SECTORSIZE

// Message Structs
type req_data struct {
//    volume  uint64    //TODO: add support for multiple volumes
    reqType uint16      // Request type
    length  uint32      // Number of sectors to read/write
    offset  uint32      // Volume offset in terms of sectors
    seqNum  uint32      // Request sequence number
}

type resp_data struct {
    status uint16   // Status
    seqNum uint32   // Request sequence number
    data []byte     // Data to send back. if read, size = BLOCKSIZE, else size = 0
}


var volume []byte  // Global for now

// Process that sends some request to VBD
func main(){
    volume = make([]byte, 1024*FBS_SECTORSIZE)    // 512KB tester volume

    // Listen to some port, can be changed
    ln, err := net.Listen("tcp", ":8080")
    if err != nil {
        return  
    }
    for {
        conn, err := ln.Accept()
        if err != nil {
            continue;    
        }
        go handleConnection(conn)
    }
}

func handleConnection(conn net.Conn){
    // Fixed size buffer
    inbuf := make([]byte, 2*FBS_SECTORSIZE)

    for {
        bytesRead, err := conn.Read(inbuf)

        fmt.Printf("Server: % X\n\n", inbuf[0:bytesRead])

        if err != nil || bytesRead < 14 {
            fmt.Println("Read Error")
            conn.Close()
            return
        }

        inMessage := unpackMessage(inbuf, bytesRead)

        fmt.Printf("Server: % X\n\n", inMessage)

        // Process message according to request type
        switch inMessage.reqType {
        case 2: // Read
            err = handleReadRequest(conn, inMessage)
        case 1: // Write   
            err = handleWriteRequest(conn, inMessage)
        default:
        }
    }

}


// Helper functions
func unpackMessage(buf []byte, bytesRead int) (msg req_data) {
    // Translate raw bytes into our local input struct because
    // go doesn't allow packed structs.
    
    binary.Read(bytes.NewBuffer(buf[0:2]), binary.BigEndian, &msg.reqType)
    binary.Read(bytes.NewBuffer(buf[2:6]), binary.BigEndian, &msg.length)
    binary.Read(bytes.NewBuffer(buf[6:10]), binary.BigEndian, &msg.offset)
    binary.Read(bytes.NewBuffer(buf[10:14]), binary.BigEndian, &msg.seqNum)

    return
}

// Send data
func sendResponse(conn net.Conn, response resp_data) (bytesWritten int, err error){
    // Translate data from our local output struct to raw bytes
//    outbuf := make([]byte, len(response.data) + 8)

    outbuf := new(bytes.Buffer)

    // Send header
    binary.Write(outbuf, binary.BigEndian, response.status)
    binary.Write(outbuf, binary.BigEndian, response.seqNum)
    
    if(len(response.data) > 0){
        binary.Write(outbuf, binary.BigEndian, response.data)
    }

    bytesWritten, err = conn.Write(outbuf.Bytes())

    return 
}

func handleReadRequest(conn net.Conn, request req_data) (err error){
    var response resp_data

    // volume[offset] is the data.
    response.status = 0
    response.seqNum = request.seqNum
    response.data = make( []byte, request.length )
    
    min := FBS_SECTORSIZE*request.offset
    max := min + FBS_SECTORSIZE*request.length
    binary.Read(bytes.NewBuffer(volume[min:max]), binary.BigEndian, &response.data)
    
    _, err = sendResponse(conn, response)

    return
}

func handleWriteRequest(conn net.Conn, request req_data) (err error){
    var response resp_data

    response.status = 0
    response.seqNum = request.seqNum
    
    // Read length * FBS_SECTORSIZE bytes from connection
    
    _, err = sendResponse(conn, response)

    return 
}


