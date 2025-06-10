//////////////////////////////////////////////////////////////////////////////////////
//
//      shm.h
//
//      Created By: Autumn Rose
//
//      CMP SCI 4760 - Operating Systems
//
//      Project 6: Paging
//
//      Due Date: 15 May 2025
//
//      Description: This header defines the structure for the simulated clock and
//                   resource descriptor - both of which will be in shared memory
//                   for both oss.cpp and worker.cpp to access, as well as the
//                   necessary libraries and variables.
//
//////////////////////////////////////////////////////////////////////////////////////

#ifndef SHM_H
#define SHM_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

#define SHM_KEY 123456
#define BUFF_SZ sizeof(ShmClock)
#define SECOND 1000000000L
#define MAX_PROCS 20
#define NUM_RSCS 5
#define NUM_RSC_INSTANCES 10

#define FRAME_COUNT 256
#define PAGE_COUNT 32
#define PAGE_SIZE 1024
#define TOTAL_MEM 131072 // 128 KB

//FRAME
struct Frame {
    bool occupied;
    pid_t pid;
    int pageNumber;
    bool dirty;
    int lastRefSec;
    int lastRefNano;
};

//SIMULATED CLOCK
struct ShmClock {
    int seconds;
    int nano;
};

//RESOURCE DESCRIPTOR
struct ResourceDescriptor {
    int totalInstances;
    int availableInstances;
    int allocated[MAX_PROCS];
};

//COMBINED SHM SEGMENT
struct ShmSegment {
    ShmClock clock;
    ResourceDescriptor resources[NUM_RSCS];
};

#endif //SHM_H
