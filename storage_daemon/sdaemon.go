package main

import (
    "net"
    "bytes"
    "encoding/binary"
)

// Definitions, types
const BLOCKSIZE uint32 = 4096   // Bytes in a block

type inputMsg struct {
    volume uint64
    reqType uint8
    offset uint64    
}

type outputMsg struct {
    data [BLOCKSIZE]byte
}



// Process that sends some request to VBD
func main(){
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
    // Unpackage request
    // Request contains: volumeID, request type, offset
    buffer := make([]byte, 1024)

    length, err := conn.Read(buffer)

    if err != nil {
        return
    }

    inMessage := unpackMessage(buffer, length)

    var outMessage outputMsg

    switch inMessage.reqType {
    case 0: // Read
        outMessage.data[0] = 0
    case 1: // Write   
        outMessage.data[0] = 0
    default:
    }

    buffer = packMessage(outMessage)

    conn.Write([]byte(buffer)[:])
    
    // Close connection
    conn.Close()
}


// Helper functions
func unpackMessage(req []byte, size int) (msg inputMsg) {
     binary.Read(bytes.NewBuffer(req[:]), binary.BigEndian, &msg)
     return
}

func packMessage(msg outputMsg) ([]byte){
    return msg.data[:]   
}

func handleReadRequest(conn net.Conn){
    return
}

