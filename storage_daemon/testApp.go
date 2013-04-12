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
    
    var msg C.struct_fbs_header
    msg.command = 1;
    msg.len = 2
    msg.offset = 1
    msg.seq_num = 5

    buffer := new(bytes.Buffer)
    
    binary.Write(buffer, binary.BigEndian, msg)

    fmt.Printf("Client Msg: % X\n", buffer.Bytes())
     
    _, err = conn.Write(buffer.Bytes())

    if err != nil {
        fmt.Println("Write Error")
        conn.Close()
        os.Exit(1)    
    }

    conn.Close()

}
