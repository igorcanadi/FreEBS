package main

import (
    "os"
    "fmt"
    "net"
    "bytes"
    "encoding/binary"
)

// Constants
const BLOCKSIZE uint32 = 4096   // Bytes in a block

// Message Structs
type inputMsg struct {
    volume  uint64
    reqType uint8
    offset  uint64    
}

type outputMsg struct {
    data    [BLOCKSIZE]byte
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
    
    buf := make([]byte, 1024)

    // TODO: Make this continually read from the connection until 
    bytesRead, err := conn.Read(buf)

    fmt.Printf("Server: % X\n\n", buf[0:bytesRead])

    if err != nil {
        fmt.Println("Read Error")
        conn.Close()
        os.Exit(1)
    }

    inMessage := unpackMessage(buf, bytesRead)

    fmt.Printf("Server: % X\n\n", inMessage)

    var outMessage outputMsg

    // Process message according to request type
    // TODO: change this to use DRBD constants
    switch inMessage.reqType {
    case 0x00: // Read
        outMessage.data[0] = 0x11 
    case 0x01: // Write   
        outMessage.data[0] = 0x22
    default:
        outMessage.data[0] = 0x33
    }

    buf = packMessage(outMessage)

    fmt.Printf("Server: % X\n\n", buf[0])

    _, err = conn.Write(buf)
    
    // Close connection
    conn.Close()
    os.Exit(0)
}


// Helper functions
func unpackMessage(buf []byte, bytesRead int) (msg inputMsg) {
    // Translate raw bytes into our local input struct because
    // go doesn't allow packed structs.
    // TODO: Change this for DRBD structs
    binary.Read(bytes.NewBuffer(buf[0:8]), binary.LittleEndian, &msg.volume)
    binary.Read(bytes.NewBuffer(buf[8:12]), binary.LittleEndian, &msg.reqType)
    binary.Read(bytes.NewBuffer(buf[12:20]), binary.LittleEndian, &msg.offset)
    
    return
}

func packMessage(msg outputMsg) ([]byte){
    // Translate data from our local output struct to raw bytes
    buf := new(bytes.Buffer)    
    
    binary.Write(buf, binary.LittleEndian, msg.data)

    return buf.Bytes()
}

func handleReadRequest(volume uint64, offset uint64) (response outputMsg){
    return
}

func handleWriteRequest(volume uint64, offset uint64, data []byte) (response outputMsg){
    return    
}


