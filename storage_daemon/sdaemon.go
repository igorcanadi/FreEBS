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
    "log"
)

// Constants
//const FBS_BLOCKSIZE uint32 = 512   // Bytes in a block
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
    volume = make([]byte, 2048*FBS_SECTORSIZE, 2048*FBS_SECTORSIZE)    // 512KB tester volume

    // Listen to some port, can be changed
    ln, err := net.Listen("tcp", ":9000")
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
    var msg req_data;

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

        //fmt.Printf("Server: % X\n\n", inbuf[0:bytesRead])

        unpackMessage(inbuf, bytesRead, &msg)

        //fmt.Printf("Server: % X\n\n", msg)

        // Process message according to request type
        switch msg.reqType {
        case 2: // Read
            err = handleReadRequest(conn, &msg)
        case 1: // Write   
            err = handleWriteRequest(conn, &msg)
        default:
        }
    }

}


// Helper functions
func unpackMessage(buf []byte, bytesRead int, msg *req_data) {
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

}

// Send data
func sendResponse(conn net.Conn, response *resp_data, write bool) (bytesWritten int, err error){
    // Translate data from our local output struct to raw bytes
//    outbuf := make([]byte, len(response.data) + 8)

    outbuf := new(bytes.Buffer)

    // Send header
    binary.Write(conn, binary.BigEndian, response.status)
    binary.Write(conn, binary.BigEndian, response.seqNum)

    if (!write) {
	    fmt.Printf("shouldn't see this")
	    conn.Write(response.data)
    }

    bytesWritten, err = conn.Write(outbuf.Bytes())

    return
}

func handleReadRequest(conn net.Conn, request *req_data) (err error){
    var response *resp_data = new(resp_data)

    // volume[offset] is the data.
    response.status = 0
    response.seqNum = request.seqNum
    response.data = make( []byte, request.length )

    min := FBS_SECTORSIZE*request.offset
    max := min + request.length

    fmt.Printf("Read: %d %d\n", min, max)

    //binary.Read(bytes.NewBuffer(volume[min:max]), binary.BigEndian, &response.data)
    response.data = volume[min:max]

    _, err = sendResponse(conn, response, false)

    return
}

func handleWriteRequest(conn net.Conn, request *req_data) (err error){
    var response *resp_data = new(resp_data)
    buffer := make([]byte, request.length)

    response.status = 0
    response.seqNum = request.seqNum

    //var i int
    var offset int
    // Read length * FBS_SECTORSIZE bytes from connectioni

    //for i, offset = 0, request.offset * FBS_SECTORSIZE; i < request.length; i, offset = i+1, offset + FBS_SECTORSIZE {
	bytesRead, err := conn.Read(buffer)
	if err != nil {
		log.Fatal("error reading")
	}
	if bytesRead <= 0 {
	    goto end
	}
	fmt.Printf("Write: Bytes to write at offset %X: % X\n", offset, buffer)
	copy(volume[offset:], buffer)    
	fmt.Printf("Write: Volume contents %X \n", volume[offset:offset+FBS_SECTORSIZE])
    //}

end:
    _, err = sendResponse(conn, response, true)

    return 
}


