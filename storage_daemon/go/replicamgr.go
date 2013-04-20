package replicamgr

/* 
#cgo CFLAGS: -I../lsvd/
#cgo LDFLAGS: -L../lsvd/
#include "msgs.h"
#include "lsvd.h"
*/
import "C"

import  (
    "unsafe"
    "errors"
    "log"
)


const LSVD_SECTORSIZE = C.SECTOR_SIZE
const FBS_SECTORSIZE = C.FBS_SECTORSIZE

type ReplicaManager struct {
    replicas []C.struct_lsvd_disk
    numReplicas uint32
    numReaders  uint32
    numWriters  uint32
}

func (r ReplicaManager) CreateReplicas(numReplicas, writes, reads uint32)(err error){
    if (numReplicas == 0 || (writes + reads) <= numReplicas){
        err = errors.New("createReplica: Bad parameters")
        return 
    }

    r.replicas = make([]C.struct_lsvd_disk, numReplicas) 
    r.numReplicas = numReplicas
    r.numWriters = writes
    r.numReaders = reads  
    return
}

/*
 * Retrieve and write data from LSVD volume
 **/
func (r ReplicaManager) Read(offset, length uint32) (buf []byte, err error){
    // Byte offsets
    FBSmin := FBS_SECTORSIZE * offset 
    FBSmax := FBSmin + length * FBS_SECTORSIZE
    LSVDmin := FBSmin - (FBSmin % LSVD_SECTORSIZE)        // Round down
    LSVDmax := FBSmax + (LSVD_SECTORSIZE - (FBSmax % LSVD_SECTORSIZE)) // Round up

    // LSVD sector offsets
    lsvd_off := LSVDmin / LSVD_SECTORSIZE   
    lsvd_len := (LSVDmax - LSVDmin) / LSVD_SECTORSIZE

    // TODO: Use threads + barrier to read R replicas, get latest version
    version := C.get_version(&r.replicas[0])
    buffer := make([]byte, lsvd_len * LSVD_SECTORSIZE)

    status := C.read_lsvd(&r.replicas[0], unsafe.Pointer(&buffer[0]), C.uint64_t(lsvd_off), C.uint64_t(lsvd_len), C.uint64_t(version))
    if (status < 0){
        log.Println("LSVD Read error")
        err = errors.New("LSVD Read error")
        return
    }

    // At this point we would cache stuff.

    // Return range of min:max for FBS
    return buffer[FBSmin:FBSmax], err
}

func (r ReplicaManager) Write(offset, length, version uint32, data []byte) (err error){
    FBSmin := FBS_SECTORSIZE * offset               // Byte offsets
    FBSmax := FBSmin + length * FBS_SECTORSIZE      

    // LSVD byte offsets
    seg1off := (FBSmin - FBSmin % LSVD_SECTORSIZE)
//    seg2off := seg1off + LSVD_SECTORSIZE
    seg3off := (FBSmax - FBSmax % LSVD_SECTORSIZE)
    
    lsvd_len := seg3off/LSVD_SECTORSIZE - seg1off/LSVD_SECTORSIZE + 1

    if (FBSmin % LSVD_SECTORSIZE) != 0 {
        // Read the data
    }
    
    status := C.write_lsvd(&r.replicas[0], unsafe.Pointer(&data[0]), C.uint64_t(lsvd_len), C.uint64_t(seg1off), C.uint64_t(version))
    if (status < 0) {
        return    
    }

    if (FBSmax % LSVD_SECTORSIZE) != 0 {
        // Read latest    
    }

    return
}


