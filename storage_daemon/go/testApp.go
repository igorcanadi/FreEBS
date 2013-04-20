package main

/*
#include "msgs.h"
*/
import "C"

import (
//    "unsafe"
    "fmt"
    "os"
    "net"
    "bytes"
    "encoding/binary"
)

func main(){
    conn, err := net.Dial("tcp", "localhost:8080")   
    if err != nil {
        fmt.Println("Error")
        os.Exit(1) 
    }

    data := make ([]byte, C.FBS_SECTORSIZE)
    data[0] = 0xFF
    data[1] = 0xAA
    data[2] = 0xBB

    writeRequest(conn, 1, 0, 5, data)
    readRequest(conn, 1, 0, 5)

    conn.Close()
}    

func sendCommand(conn net.Conn, command uint16, length uint32, offset uint32, seq_num uint32){
    buffer := new(bytes.Buffer) 

/*
    var msg C.struct_fbs_header
        
    msg.command = command    // Command (read/write)
    msg.len =  length        // Sectors
    msg.offset = offset      // Offset
    msg.seq_num = seq_num    // Sequence number
*/
    binary.Write(buffer, binary.BigEndian, command)
    binary.Write(buffer, binary.BigEndian, length)
    binary.Write(buffer, binary.BigEndian, offset)
    binary.Write(buffer, binary.BigEndian, seq_num)

    fmt.Printf("Client Send: % X\n", buffer.Bytes())
     
    _, err := conn.Write(buffer.Bytes())

    if err != nil {
        fmt.Println("Write Error")
        return
    }
}

func waitResponse(conn net.Conn, len uint32){
     // Listen on connection for a response
    var resp C.struct_fbs_response
    size := len * C.FBS_SECTORSIZE + 8
    inbuf := make( []byte, size)

    _, err := conn.Read(inbuf)

    if err != nil {
        fmt.Println("Read Error")
        return    
    }
    binary.Read(bytes.NewBuffer(inbuf), binary.BigEndian, resp.status)
    binary.Read(bytes.NewBuffer(inbuf), binary.BigEndian, resp.seq_num)


    fmt.Printf("Client Receive: % X \t % X\n", resp, inbuf)
   
    
}

func readRequest(conn net.Conn, len, offset, seq_num uint32) {

    sendCommand(conn, 2, len, offset, seq_num)
    waitResponse(conn, len)
}

func writeRequest(conn net.Conn, len, offset, seq_num uint32, data []byte){
    sendCommand(conn, 1, len, offset, seq_num)
    
    // Write to connection with data
    conn.Write(data)
    
    // Listen for response
    waitResponse(conn, len)
}
