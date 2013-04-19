package replicamgr

/*
#include "msgs.h"
#include "../lsvd/lsvd.h"
*/
import "C"

const LSVD_SECTORSIZE = C.SECTOR_SIZE

type ReplicaManager struct {
    replicas []C.struct_lsvd_disk
    numReplicas uint
    numReaders  uint
    numWriters  uint
}

func (r ReplicaManager) createReplicas(numReplicas, writes, reads uint)(err error){
    if (numReplicas == 0 || (writes + reads) <= numReplicas){
        err.New("createReplica: Bad parameters")
        return 
    }

    r.replicas = make([]C.struct_lsvd_disk, numReplicas) 
    r.numReplicas = numReplicas
    r.numWriters = writes
    r.numReader = reads  
    return
}

/*
 * Retrieve and write data from LSVD volume
 **/
func (r ReplicaManager) read(offset, length uint32) (buf []byte, err error){
    // Byte offsets
    FBSmin := FBS_SECTORSIZE * offset 
    FBSmax := FBSmin + length * FBS_SECTORSIZE
    LSVDmin := FBSmin - (FBSmin % LSVD_SECTORSIZE)        // Round down
    LSVDmax := FBSmax + (LSVD_SECTORSIZE - (FBSmax % LSVD_SECTORSIZE)) // Round up

    // LSVD sector offsets
    lsvd_off := LSVDmin / LSVD_SECTORSIZE   
    lsvd_len := (LSVDmax - LSVDmin) / LSVD_SECTORSIZE

    // TODO: Use threads + barrier to read R replicas, get latest version
    version := C.get_version(r.replicas[0])
    buffer := make([]byte, lsvd_len * LSVD_SECTORSIZE)

    status := C.read_lsvd(r.replicas[0], buffer, lsvd_off, lsvd_len, version)
    if (status < 0){
        log.Println("LSVD Read error")
        err = error.New("LSVD Read error")
        return
    }

    // At this point we would cache stuff.

    // Return range of min:max for FBS
    return buffer[FBSmin:FBSmax], err
}

func (r ReplicaManager) write(offset, length uint32, data []byte) (err error){
    buffer := make([]byte, set3off - seg1off)// Initialize buffer
    FBSmin := FBS_SECTORSIZE * offset               // Byte offsets
    FBSmax := FBSmin + length * FBS_SECTORSIZE      

    // LSVD byte offsets
    seg1off := (FBSmin - FBSmin % LSVD_SECTORSIZE)
    seg2off := seg1off + LSVD_SECTORSIZE
    seg3off := (FBSmax - FBSmax % LSVD_SECTORSIZE)
    
    if (FBSmin % LSVD_SECTORSIZE) != 0 {
        // Read the data
    }
    
//    status = C.lsvd_write()

    if (FBSmax % LSVD_SECTORSIZE) != 0 {
        // Read latest    
    }

    return
}


func asyncWriter()
