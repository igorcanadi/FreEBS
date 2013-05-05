#include <stdio.h>
#include <stdlib.h>

#include "replicamgr.h"

int main(){
    uint64_t seq_num = 1;
    uint64_t off, length;
    char *write, *read;   
    bool fail = false;

    ReplicaManager r;
    r.create("/tmp/tmp.dsk", GB / LSVD_SECTORSIZE);
   
//    r.open("/tmp/tmp.dsk");

    for(length = 1; length < 32; length ++){
       write = new char[FBS_SECTORSIZE * length];
       read = new char[FBS_SECTORSIZE * length];

       for(off = 8; off < 16; off++, seq_num++){
            for(unsigned i = 0; i < length * FBS_SECTORSIZE; i++){
                write[i] = rand() % 26 + 'A';
            }

            r.write(off, length, seq_num, write);
            r.read(off, length, seq_num, read);

            for(unsigned i = 0; i < length * FBS_SECTORSIZE; i++){
                if(read[i] != write[i]){
                    printf("Failed: off=%lu i=%u %c %c\n", off, i, write[i], read[i]);
                    fail = true;
                    break;
                }
            }
        }
        
        delete write;
        delete read;
    }
    
    if (!fail){
        printf("All tests passed!\n");
    }

    return 0;
}
