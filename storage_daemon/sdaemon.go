package main

import (
    "os"
    "net"
    "fmt"
)

// Process that sends some request to VBD
func main(){
    // Listen to some port, can be changed
    ln, err := net.Listen("tcp", ":8080")
    if err != nil {
        exit()   
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
    // Request contains: volume_address, read/write, offset

    volumeAddr, err := unpackRequest()

    // Connect to appropriate VBD
    conn, err := net.Dial("tcp", volumeAddr)
    if err != nil {
        // Error connecting to VBD
        conn.Close()
        return
    }

    

    // Close connection
    conn.Close()
}

// Helper functions
func unPack(){
        
}


func handleReadRequest(conn net.Conn){
    // Send read request to 
}
