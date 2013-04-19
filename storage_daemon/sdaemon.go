package main

/*
#include "msgs.h"
*/
import "C"

// C libraries above this point.

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"unsafe"
)

// Constants
const FBS_SECTORSIZE = C.FBS_SECTORSIZE

// Message Structs
type req_data struct {
	reqType uint16 // Request type
	length  uint32 // Number of sectors to read/write
	offset  uint32 // Volume offset in terms of sectors
	seqNum  uint32 // Request sequence number
}

type resp_data struct {
	status uint16 // Status
	seqNum uint32 // Request sequence number
	data   []byte // Data to send back. if read, size = BLOCKSIZE, else size = 0
}

var volume []byte // Global for now

// Process that sends some request to VBD
func main() {
	volume = make([]byte, 2048*FBS_SECTORSIZE, 2048*FBS_SECTORSIZE) // tester volume

	// Listen to some port, can be changed
	ln, err := net.Listen("tcp", ":9000")
	if err != nil {
		return
	}
	for {
		conn, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleConnection(conn)
	}
}

func handleConnection(conn net.Conn) {
	// Fixed size buffer
	inbuf := make([]byte, 14)
	var msg req_data

	for {
		bytesRead, err := conn.Read(inbuf)

		if bytesRead < 14 {
			continue
		}

		if err != nil {
			fmt.Println("Read Error, bytes read: ", bytesRead)
			conn.Close()
			return
		}

		unpackMessage(inbuf, bytesRead, &msg)

		// Process message according to request type
		switch msg.reqType {
		case 2: // Read
			err = handleReadRequest(conn, &msg)
		case 1: // Write   
			err = handleWriteRequest(conn, &msg)
		default:
		}
		if err != nil {
			log.Fatal(err)
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

}

// Send data
func sendResponse(conn net.Conn, response *resp_data, write bool) (bytesWritten int, err error) {
	// Send header
	err = binary.Write(conn, binary.BigEndian, response.status)
	if err != nil {
		return 0, err
	}
	err = binary.Write(conn, binary.BigEndian, response.seqNum)
	if err != nil {
		return 0, err
	}

	if !write {
		bytesWritten, err = conn.Write(response.data)
		if err != nil {
			return bytesWritten, err
		}
	}
	log.Println("sent response")

	return 0, nil
}

func handleReadRequest(conn net.Conn, request *req_data) (err error) {
	var response *resp_data = new(resp_data)

	response.status = 0
	response.seqNum = request.seqNum

	min := FBS_SECTORSIZE * request.offset
	max := min + request.length

	fmt.Printf("Read: %d %d\n", min, max)

	response.data = volume[min:max]

	_, err = sendResponse(conn, response, false)

	return
}

func handleWriteRequest(conn net.Conn, request *req_data) (err error) {
	var response *resp_data = new(resp_data)

	response.status = 0
	response.seqNum = request.seqNum

	// Read length * FBS_SECTORSIZE bytes from connectioni
	var req_offset, vol_offset uint32
	for req_offset, vol_offset = 0, request.offset*FBS_SECTORSIZE; req_offset < request.length; {
		bytesRead, err := conn.Read(volume[vol_offset : vol_offset+(request.length-req_offset)])
		if err != nil {
			log.Fatal(err)
			goto end
		}
		req_offset += uint32(bytesRead)
		vol_offset += uint32(bytesRead)
	}

end:
	log.Println("Write: sending response")
	_, err = sendResponse(conn, response, true)
	if err != nil {
		log.Fatal(err)
	}

	return
}
