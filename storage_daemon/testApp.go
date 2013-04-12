package main

/*
#include "msgs.h"
*/
import "C"

import (
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
    
    buffer := new(bytes.Buffer) // Manually making buffer

    var msg C.struct_fbs_header
        
    msg.command = 2 // Read
    msg.len = 1     // Sectors
    msg.offset = 0  // Offset
    msg.seq_num = 5 

    binary.Write(buffer, binary.BigEndian, msg.command)
    binary.Write(buffer, binary.BigEndian, msg.len)
    binary.Write(buffer, binary.BigEndian, msg.offset)
    binary.Write(buffer, binary.BigEndian, msg.seq_num)

    fmt.Printf("Client Msg: % X\n", buffer.Bytes())
     
    _, err = conn.Write(buffer.Bytes())

    if err != nil {
        fmt.Println("Write Error")
        conn.Close()
        os.Exit(1)    
    }

    conn.Close()

}
