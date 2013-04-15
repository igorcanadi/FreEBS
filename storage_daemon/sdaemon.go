package main

/*
#include "msgs.h"
 */
import "C"
// C libraries above this point.

import (
    "unsafe"
//    "os"
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
    inbuf := make([]byte, 14)

    for {
        bytesRead, err := conn.Read(inbuf)

        
        if bytesRead < 14 {
            continue;
        }

        if err != nil {
            fmt.Println("Read Error, bytes read: ", bytesRead)
            conn.Close()
            return
        }

        fmt.Printf("Server: % X\n\n", inbuf[0:bytesRead])

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
    var dummy C.struct_fbs_header
    commandHi := unsafe.Sizeof(dummy.command)
    lengthHi := commandHi + unsafe.Sizeof(dummy.len)
    offsetHi := lengthHi + unsafe.Sizeof(dummy.offset)

    binary.Read(bytes.NewBuffer(buf[0:commandHi]), binary.BigEndian, &msg.reqType)
    binary.Read(bytes.NewBuffer(buf[commandHi:lengthHi]), binary.BigEndian, &msg.length)
    binary.Read(bytes.NewBuffer(buf[lengthHi:offsetHi]), binary.BigEndian, &msg.offset)
    binary.Read(bytes.NewBuffer(buf[offsetHi:bytesRead]), binary.BigEndian, &msg.seqNum)

//    fmt.Println("Request: ", msg.reqType, msg.length, msg.offset, msg.seqNum)

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
    response.data = make( []byte, request.length*FBS_SECTORSIZE )
    
    min := FBS_SECTORSIZE*request.offset
    max := min + FBS_SECTORSIZE*request.length

    fmt.Printf("Server: %d %d % X", min, max,volume[min:max])
    
    binary.Read(bytes.NewBuffer(volume[min:max]), binary.BigEndian, &response.data)

    _, err = sendResponse(conn, response)

    return
}

func handleWriteRequest(conn net.Conn, request req_data) (err error){
    var response resp_data
    buffer := make([]byte, FBS_SECTORSIZE)

    response.status = 0
    response.seqNum = request.seqNum
    
    var i uint32
    var offset uint32
    // Read length * FBS_SECTORSIZE bytes from connectioni

    for i, offset = 0, request.offset * FBS_SECTORSIZE; i < request.length; i, offset = i+1, offset + FBS_SECTORSIZE {
        bytesRead, err := conn.Read(buffer)
        if bytesRead <= 0 {
            i--    
            continue
        }
        if err != nil {
            break    
        }
        fmt.Printf("Server: Bytes to write at offset %X: % X\n", offset, buffer)
        copy(volume[offset:offset+FBS_SECTORSIZE], buffer)    
        fmt.Printf("Server: Volume contents %X \n", volume[offset:offset+FBS_SECTORSIZE])
    }

    _, err = sendResponse(conn, response)

    return 
}


