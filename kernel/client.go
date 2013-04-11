package main

import (
	"net"
	"log"
	"fmt"
)

func handleConnection(conn net.Conn) {
	fmt.Println("got connection.")
}

func main() {
	ln, err := net.Listen("tcp", ":9000")
	if err != nil {
		log.Fatal(err)
	}
	for {
		fmt.Println("waiting");
		conn, err := ln.Accept()
		fmt.Println("connecting")
		if err != nil {
			log.Fatal(err)
		}
		go handleConnection(conn)
	}
}
