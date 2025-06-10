//////////////////////////////////////////////////////////////////////////////////////
//
//      worker.cpp
//
//      Created By: Autumn Rose
//
//      CMP SCI 4760
//
//      Project 6: Paging
//
//      Due Date: 15 May 2025
//
//      Description: This file is executed by oss and simulates doing work and
//              memory management via paging. Worker will decide whether to read
//              or write and send a message to oss with its request, and wait for
//              its memory request to be granted.
//
//////////////////////////////////////////////////////////////////////////////////////

// INCLUDED LIBARIES/NAMESPACE
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <cstdlib>
#include <cstring>
#include "shm.h"
#include "msgq.h"
using namespace std;

#define PAGE_COUNT 32
#define PAGE_SIZE 1024
#define ADDRESS_SPACE (PAGE_COUNT * PAGE_SIZE) // 32768
#define READ_CHANCE 80
#define TERMINATE_INTERVAL 1000

// MAIN
int main(int argc, char** argv) {
    int shmid;
    msgBuffer buf;
    buf.mtype = 1;
    int msqid = 0;
    key_t key;
    bool terminated = false;
    int action;
    int accessCount = 0;
    int terminateThreshold = 20 + (rand() % 5); // for testing
    int page;
    int offset;
    int address;

    // SETUP ACCESS TO MESSAGE QUEUE
    if ((key = ftok("msgq.txt", 1)) == -1) { //get key
        cerr << "ERROR: ftok in worker " << getpid() << endl;
        exit(1);
    }
    if ((msqid = msgget(key, PERMS)) == -1) { //create queue
        cerr << "ERROR: msgget in worker " << getpid() << endl;
        exit(1);
    }

    // ATTACH TO SHARED MEMORY TO VIEW CLOCK
    shmid = shmget(SHM_KEY, BUFF_SZ, 0666);
    if (shmid == -1) {
        cerr << "ERROR: Failed to get shared memory in worker." << strerror(errno) << endl;
        exit(1);
    }
    ShmSegment* shmData = (ShmSegment*)shmat(shmid, NULL, 0);
    if (shmData == (void*)-1) {
        cerr << "ERROR: Failed to attach to shared memory in worker." << endl;
        exit(1);
    }

    while (!terminated) {
        //GENERATE RANDOM VIRTUAL ADDRESS (0-32767)
        page = rand() % PAGE_COUNT;
        offset = rand() % PAGE_SIZE;
        address = (page * PAGE_SIZE) + offset;

        //DETERMINE READ OR WRITE
        action = (rand() % 100 < READ_CHANCE) ? 0 : 1;

        //SEND MESSAGE
        buf.mtype = 1;
        buf.pid = getpid();
        buf.address = address;
        buf.action = action;
        buf.status = 0;

        if (msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
            cerr << "ERROR: msgsnd memory request failed\n";
            exit(1);
        }

        //WAIT FOR RESPONSE
        if (msgrcv(msqid, &buf, sizeof(msgBuffer), getpid(), 0) == -1) {
            cerr << "ERROR: msgrcv response failed\n";
            exit(1);
        }
        accessCount++;
        cout << "Worker " << getpid() << " accessCount: " << accessCount << " / " << terminateThreshold << endl;

        //CHECK WHETHER TO TERMINATE
        if (accessCount >= terminateThreshold) {
            cout << "Worker " << getpid() << " hit threshold with accessCount = " << accessCount << endl;
            int termChance = rand() % 100;
            if (termChance < 20) {
                cout << "WORKER " << getpid() << " deciding to terminate.\n";
                buf.mtype = 1;
                buf.pid = getpid();
                buf.status = -1;
                cout << "WORKER " << getpid() << " sending termination msg.\n";
                if (msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
                    cerr << "ERROR: msgsnd termination failed\n";
                    exit(1);
                }

                terminated = true;
            }
            else {
                //RESET ACCESS COUNT
                accessCount = 0;
                terminateThreshold = 1000 + (rand() % 201);
            }
        }
    }

    // DETACH FROM SHARED MEMORY
    shmdt(shmData);
    return 0;
}
