package main

import (
    "fmt"
    "os"
    "net"
    "bytes"
    "encoding/binary"
)

type MyMsg struct {
    volume uint64
    reqType uint8
    offset uint64
}

func main(){
    conn, err := net.Dial("tcp", "localhost:8080")   
    if err != nil {
        fmt.Println("Error")
        os.Exit(1) 
    }
    
    var msg MyMsg
    msg.volume = 0xFFFF00FF
    msg.reqType = 0xCC
    msg.offset = 0xBB

    buffer := new(bytes.Buffer)
    
    binary.Write(buffer, binary.LittleEndian, msg)

    fmt.Printf("Client Msg: % X\n", buffer.Bytes())
     
    _, err = conn.Write(buffer.Bytes())

    if err != nil {
        fmt.Println("Write Error")
        conn.Close()
        os.Exit(1)    
    }

    conn.Close()

}
