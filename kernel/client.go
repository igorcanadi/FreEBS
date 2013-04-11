package main

import (
	"net"
	"log"
	"fmt"
	"bufio"
)

type header struct {
	command uint16
	length uint32
	sector uint64
	seq_num uint32
	dp_flags uint32
}

func handleConnection(conn net.Conn) {
	fmt.Println("got connection.")
	b := bufio.NewReader(conn)
	var buf []byte = make([]byte, sizeof(header))
	for {
		b.Read(buf)
	}
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
