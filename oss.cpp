//////////////////////////////////////////////////////////////////////////////////////
//
//      oss.cpp
//
//      Created By: Autumn Rose
//
//      CMP SCI 4760 - Operating Systems
//
//      Project 6: Paging
//
//      Due Date: 15 May 2025
//
//      Description: This file parses command line arguments, initializes a simulated
//                   system clock and frame table in shared memory, and creates
//                   a process table for which it will maintain while launching workers
//                   (see worker.cpp) within the given parameters given by the accepted
//                   arguments.
//
//////////////////////////////////////////////////////////////////////////////////////

// INCLUDED LIBRARIES/NAMESPACE
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <signal.h>
#include <fstream>
#include <queue>
#include <climits>
#include "shm.h"
#include "msgq.h"
using namespace std;

#define MAX_PROCS 20

// FRAME TABLE
Frame frames[FRAME_COUNT];

// LOG TRACKING
int logLinesWritten = 0;
const int MAX_LOG_LINES = 10000;

// MEMORY TRACKING
int totalMemoryAccesses = 0;
int totalPageFaults = 0;
time_t startWallTime;

// PROCESS CONTROL BLOCK / PROCESS TABLE
struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int messagesSent;
    int pageTable[PAGE_COUNT];
    int memoryAccesses;
};
struct PCB processTable[20];

// SHARED MEMORY
int shmid;
ShmSegment* shmData = nullptr;
int msqid;

// FUNCTION PROTOTYPES
void printUsage();
void incrementClock();
void updatePCBofTerminatedChild(pid_t);
void outputProcessTable();
void signal_handler(int);
int countActiveChildren();
int openSpaceInTable();
bool timeToLaunchProcess(int, int, int);
int findProcessIndex(pid_t);
void outputFrameTable(ofstream&);

// QUEUE STRUCTURES AND DEFINITIONS
struct BlockedProcess {
    pid_t pid;
    int processIndex;
    int page;
    int address;
    int action;
    int frameIndex;
    int unblockAtSec;
    int unblockAtNano;
};
vector<BlockedProcess> blockedQueue;

// MAIN
int main(int argc, char** argv) {
    // VARIABLES FOR COMMAND LINE PARSING
    int proc = 0;
    int simul = 0;
    int intervalInMsToLaunchChildren = 0;
    string logfile;
    int opt;
    // VARIABLES FOR INTERMITTENTLY OUTPUTTING THE PROCESS TABLE
    int lastPrintTimeSec = 0;
    int lastPrintTimeNano = 0;
    int elapsedPrintTimeSec;
    int elapsedPrintTimeNano;
    int elapsedTotalTimeNano;
    // VARIABLES FOR LAUNCHING NEW PROCESS
    int launchedChildren = 0;
    int activeChildren;
    int lastLaunchSec = 0;
    int lastLaunchNano = 0;
    int openIndex;
    int status;
    pid_t launchedPid;
    time_t launchingStartTime = time(NULL);
    // VARIABLES FOR MESSAGE PASSING
    int currentIndex = 0;
    int startIndex;
    bool found = false;
    msgBuffer buf;
    key_t key;
    system("touch msgq.txt");
    pid_t childMessaging;
    int totalMessagesSent = 0;
    int termPid;
    pid_t terminatedPid;
    // VARIABLES FOR RESOURCE MANAGEMENT
    int processIndex;
    // VARIABLES FOR BLOCKED PROCESSES
    int idx;
    int rsc;
    // VARIABLES FOR LOGFILE SETTINGS
    bool verbose = true;
    // VARIABLES FOR PAGING
    int address;
    int action;
    pid_t pid;
    int page;
    int offset;
    int frameIndex;
    int delayNano;
    int selectedFrame;
    long long oldestTime;
    long long frameTime;
    int oldPid;
    int oldPage;
    int oldIndex;
    startWallTime = time(NULL);
    int accesses;
    float effectiveTime;
    long long unblockTime;
    long long timeNow;
    int unblockSec;
    int unblockNano;
    int frameIdx;
    int fIdx;
    int normalTerminations = 0;

    // INITIALIZE TIMEOUT SIGNAL
    signal(SIGALRM, signal_handler);
    alarm(5);

    // BEGIN PARSING COMMAND LINE ARGUMENTS USING FLAGS
    while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1) {
        switch (opt) {
        case 'h': // h - display help/usage message and exit (success)
            printUsage();
            return 0;
        case 'n': // n flag - store total children to launch
            proc = atoi(optarg);
            break;
        case 's': // s flag - store number of children able to run simultaneously
            simul = atoi(optarg);
            break;
        case 'i': // i flag - store interval to launch children
            intervalInMsToLaunchChildren = atoi(optarg);
            break;
        case 'f': // f flag - store name for output file
            logfile = optarg;
            break;
        default: // default case - display usage and exit
            cerr << "Usage Invalid.\n";
            printUsage();
            return 1;
        }
    }

    // VALIDATING COMMAND LINE ARGUMENTS
    if (proc <= 0 || simul <= 0 || intervalInMsToLaunchChildren <= 0) {
        cerr << "ERROR: All arguments for flags -n, -s, and -i must be positive integers.\n";
        printUsage();
        return 1;
    }

    // ENFORCE MAX PROCS AS 100
    if (proc > 100) {
        proc = 100;
    }

    // ENFORCE MAX SIMUL PROCS AS 18
    if (simul > 18) {
        simul = 18;
    }

    // CREATE LOG FILE WITH SPECIFIED NAME
    ofstream file(logfile);
    if (!file) {
        cerr << "ERROR: log file could not be opened" << endl;
        exit(1);
    }

    // INITIALIZE FRAME TABLE
    for (int i = 0; i < FRAME_COUNT; i++) {
        frames[i].occupied = false;
        frames[i].dirty = false;
        frames[i].pid = -1;
        frames[i].pageNumber = -1;
        frames[i].lastRefSec = 0;
        frames[i].lastRefNano = 0;
    }

    // INITIALIZE SIMULATED CLOCK IN SHARED MEMORY
    shmid = shmget(SHM_KEY, BUFF_SZ, IPC_CREAT | 0666);
    if (shmid == -1) {
        cerr << "ERROR: Failed to create shared memory." << endl;
        exit(1);
    }
    shmData = (ShmSegment*)shmat(shmid, NULL, 0);
    if (shmData == (void*)-1) {
        cerr << "ERROR: Failed to attach to shared memory." << endl;
        exit(1);
    }
    shmData->clock.seconds = 0;
    shmData->clock.nano = 0;

    // INITIALIZE MESSAGE PASSING
    if ((key = ftok("msgq.txt", 1)) == -1) { //get key
        cerr << "ERROR: ftok in oss \n";
        exit(1);
    }
    if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) { //create msgq
        cerr << "ERROR: msgget in oss \n";
        exit(1);
    }

    //---------------------------------------------------------------
    // ENTER LOOP TO MAINTAIN CLOCK/PROCESS TABLE AND LAUNCH CHILDREN
    while (launchedChildren < proc || countActiveChildren() > 0) {
        incrementClock();
        //HANDLE RECEIVED MESSAGES AND PAGING
        while (msgrcv(msqid, &buf, sizeof(msgBuffer), 1, IPC_NOWAIT) != -1) {
            address = buf.address;
            action = buf.action;
            pid = buf.pid;
            page = address / PAGE_SIZE;
            offset = address % PAGE_SIZE;
            processIndex = findProcessIndex(buf.pid);
            frameIndex = processTable[processIndex].pageTable[page];

            cout << "OSS: P" << processIndex << " requesting " << (action == 0 ? "read" : "write") << " of address " << address << " at time " << shmData->clock.seconds << ":" << shmData->clock.nano << endl;
            if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                file << "OSS: P" << processIndex << " requesting " << (action == 0 ? "read" : "write") << " of address " << address << " at time " << shmData->clock.seconds << ":" << shmData->clock.nano << endl;
                logLinesWritten++;
            }
            if (frameIndex != -1 && frames[frameIndex].occupied && frames[frameIndex].pid == pid && frames[frameIndex].pageNumber == page) {
                //PAGE ALREADY IN MEMORY
                frames[frameIndex].lastRefSec = shmData->clock.seconds;
                frames[frameIndex].lastRefNano = shmData->clock.nano;
                if (action == 1) {
                    frames[frameIndex].dirty = true;
                }
                totalMemoryAccesses++;
                processTable[processIndex].memoryAccesses++;

                //INCREMENT CLOCK
                shmData->clock.nano += 100;
                if (shmData->clock.nano >= SECOND) {
                    shmData->clock.seconds++;
                    shmData->clock.nano -= SECOND;
                }

                //SEND RESPONSE TO WORKER
                buf.mtype = pid;
                buf.status = 0;
                cout << "OSS page hit for P" << processIndex << ", frame " << frameIndex << ". Sending response.\n";
                if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                    file << "OSS: page hit for P" << processIndex << ", frame " << frameIndex << ". Sending response.\n";
                    logLinesWritten++;
                }
                if (msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
                    cerr << "OSS: msgsnd response failed\n";
                    exit(1);
                }

                cout << "OSS: Address " << address << " in frame " << frameIndex << ", giving data to P" << processIndex << " at time " << shmData->clock.seconds << ":" << shmData->clock.nano << endl;
                if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                    file << "OSS: Address " << address << " in frame " << frameIndex << ", giving data to P" << processIndex << " at time " << shmData->clock.seconds << ":" << shmData->clock.nano << endl;
                    logLinesWritten++;
                }
            }
            else {
                //PAGE FAULT
                cout << "OSS: Address " << address << " is not in a frame, pagefault" << endl;
                if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                    file << "OSS: Address " << address << " is not in a frame, pagefault" << endl;
                    logLinesWritten++;
                }
                delayNano = 14 * 1000000;
                shmData->clock.nano += delayNano;
                if (shmData->clock.nano >= SECOND) {
                    shmData->clock.seconds += shmData->clock.nano / SECOND;
                    shmData->clock.nano %= SECOND;
                }
                totalMemoryAccesses++;
                totalPageFaults++;

                //FIND OPEN FRAME
                selectedFrame = -1;
                for (int i = 0; i < FRAME_COUNT; ++i) {
                    if (!frames[i].occupied) {
                        selectedFrame = i;
                        break;
                    }
                }

                if (selectedFrame == -1) {
                    oldestTime = LLONG_MAX;
                    for (int i = 0; i < FRAME_COUNT; ++i) {
                        frameTime = ((long long)frames[i].lastRefSec * SECOND) + frames[i].lastRefNano;
                        if (frameTime < oldestTime) {
                            oldestTime = frameTime;
                            selectedFrame = i;
                        }
                    }

                    cout << "OSS: Clearing frame " << selectedFrame << " and swapping in P" << processIndex << " page " << page << endl;
                    if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                        file << "OSS: Clearing frame " << selectedFrame << " and swapping in P" << processIndex << " page " << page << endl;
                        logLinesWritten++;
                    }

                    if (frames[selectedFrame].dirty) {
                        cout << "OSS: Dirty bit of frame " << selectedFrame << " set, adding additional time to the clock" << endl;
                        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                            file << "OSS: Dirty bit of frame " << selectedFrame << " set, adding additional time to the clock" << endl;
                            logLinesWritten++;
                        }
                        shmData->clock.nano += delayNano;
                        if (shmData->clock.nano >= SECOND) {
                            shmData->clock.seconds += shmData->clock.nano / SECOND;
                            shmData->clock.nano %= SECOND;
                        }
                    }

                    //CLEAR OLD PAGE ENTRY
                    oldPid = frames[selectedFrame].pid;
                    oldPage = frames[selectedFrame].pageNumber;
                    oldIndex = findProcessIndex(oldPid);
                    if (oldIndex != -1) {
                        processTable[oldIndex].pageTable[oldPage] = -1;
                    }

                    if (frames[selectedFrame].dirty) {
                        cout << "OSS: Swapped out dirty frame. Adding 14ms I/O delay." << endl;
                        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                            file << "OSS: Swapped out dirty frame. Adding 14ms I/O delay." << endl;
                            logLinesWritten++;
                        }
                        shmData->clock.nano += delayNano;
                        if (shmData->clock.nano >= SECOND) {
                            shmData->clock.seconds += shmData->clock.nano / SECOND;
                            shmData->clock.nano %= SECOND;
                        }
                    }
                }

                //SCHEDULE UNBLOCK TIME
                unblockNano = shmData->clock.nano + 14 * 1000000;
                unblockSec = shmData->clock.seconds;
                if (unblockNano >= SECOND) {
                    unblockSec += unblockNano / SECOND;
                    unblockNano %= SECOND;
                }

                //CREATE BLOCKED MEMORY REQUEST
                BlockedProcess bp;
                bp.pid = pid;
                bp.processIndex = processIndex;
                bp.page = page;
                bp.address = address;
                bp.action = action;
                bp.frameIndex = selectedFrame;
                bp.unblockAtSec = unblockSec;
                bp.unblockAtNano = unblockNano;
                blockedQueue.push_back(bp);
                cout << "OSS: Queued page load for PID " << pid << " into frame " << selectedFrame << ", will unblock at " << unblockSec << ":" << unblockNano << endl;
                if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                    file << "OSS: Queue page load for PID " << pid << " into frame " << selectedFrame << ", will unblock at " << unblockSec << ":" << unblockNano << endl;
                    logLinesWritten++;
                }

                //ASSIGN FRAME TO THIS PAGE
                frames[selectedFrame].occupied = true;
                frames[selectedFrame].pid = pid;
                frames[selectedFrame].pageNumber = page;
                frames[selectedFrame].dirty = (action == 1);
                frames[selectedFrame].lastRefSec = shmData->clock.seconds;
                frames[selectedFrame].lastRefNano = shmData->clock.nano;
                processTable[processIndex].pageTable[page] = selectedFrame;
                cout << "OSS: Loaded page " << page << " for PID " << pid << " into frame " << selectedFrame << endl;
                if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                    file << "OSS: Loaded page " << page << " for PID " << pid << " into frame " << selectedFrame << endl;
                    logLinesWritten++;
                }

                //RESPOND TO WORKER
                buf.mtype = pid;
                buf.status = 0;
                if (msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
                    cerr << "OSS: msgsnd response after page fault failed\n";
                    exit(1);
                }
                        }

                        //HANDLE TERMINATING PROCESSES
                        if (buf.status == -1) {
                            processIndex = findProcessIndex(buf.pid);
                            accesses = processTable[processIndex].memoryAccesses;
                            effectiveTime = accesses > 0 ? ((float)accesses * 100) / 1000000000 : 0.0f;
                            cout << "OSS: PID " << buf.pid << " is terminating. Releasing all frames. Effective memory access time: " << effectiveTime << " sec" << endl;
                            if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                                file << "OSS: PID " << buf.pid << " terminating. Releasing all frames. Effective memory access time: " << effectiveTime << " sec" << endl;
                                logLinesWritten++;
                            }
                            for (int i = 0; i < PAGE_COUNT; i++) {
                                frameIdx = processTable[processIndex].pageTable[i];
                                if (frameIdx != -1 && frames[frameIdx].occupied && frames[frameIdx].pid == buf.pid) {
                                    frames[frameIdx].occupied = false;
                                    frames[frameIdx].dirty = false;
                                    frames[frameIdx].pid = -1;
                                    frames[frameIdx].pageNumber = -1;
                                    frames[frameIdx].lastRefSec = 0;
                                    frames[frameIdx].lastRefNano = 0;
                                    processTable[processIndex].pageTable[i] = -1;
                                }
                            }
                            updatePCBofTerminatedChild(buf.pid);
                            normalTerminations++;
                            waitpid(buf.pid, NULL, 0);
                        }
                        incrementClock();
                }

                //UNBLOCK ANY READY PROCESSES
                for (auto it = blockedQueue.begin(); it != blockedQueue.end(); ) {
                    timeNow = (long long)shmData->clock.seconds * SECOND + shmData->clock.nano;
                    unblockTime = (long long)it->unblockAtSec * SECOND + it->unblockAtNano;

                    //check if time to unblock
                    if (timeNow >= unblockTime) {
                        fIdx = it->frameIndex;

                        //set frame
                        frames[fIdx].occupied = true;
                        frames[fIdx].pid = it->pid;
                        frames[fIdx].pageNumber = it->page;
                        frames[fIdx].dirty = (it->action == 1);
                        frames[fIdx].lastRefSec = shmData->clock.seconds;
                        frames[fIdx].lastRefNano = shmData->clock.nano;
                        processTable[it->processIndex].pageTable[it->page] = fIdx;
                        //send message back
                        buf.mtype = it->pid;
                        buf.pid = it->pid;
                        buf.status = 0;
                        buf.address = it->address;
                        buf.action = it->action;
                        cout << "OSS unblocking P" << processIndex << " after page load\n";
                        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                            file << "OSS: unblocking P" << processIndex << " after page load\n";
                            logLinesWritten++;
                        }
                        if (msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
                            cerr << "OSS: msgsnd failed on unblock\n";
                            exit(1);
                        }
                        cout << "OSS: Indicating to P" << it->processIndex << " that " << (it->action == 0 ? "read" : "write") << " has happened to address " << it->address << endl;
                        cout << "OSS: Finished page load for PID " << it->pid << " (page " << it->page << ") into frame " << fIdx << endl;
                        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                            file << "OSS: Indicating to P" << it->processIndex << " that " << (it->action == 0 ? "read" : "write") << " has happened to address " << it->address << endl;
                            logLinesWritten++;
                            file << "OSS: Finished page load for PID " << it->pid << " (page " << it->page << ") into frame " << fIdx << endl;
                            logLinesWritten++;
                        }
                    }
                    else {
                        ++it;
                    }
                    it = blockedQueue.erase(it);
                }

                //CLEAN UP ANY TERMINATED PROCESSES
                while ((terminatedPid = waitpid(-1, &status, WNOHANG)) > 0) {
                    cout << "OSS: Detected terminated child with PID " << terminatedPid << endl;
                    if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
                        file << "OSS: Detected terminated child with PID " << terminatedPid << endl;
                        logLinesWritten++;
                    }
                    updatePCBofTerminatedChild(terminatedPid);
                    normalTerminations++;
                }

                //OUTPUT PROCESS TABLE EVERY HALF SECOND
                elapsedPrintTimeSec = shmData->clock.seconds - lastPrintTimeSec;
                elapsedPrintTimeNano = shmData->clock.nano - lastPrintTimeNano;
                if (elapsedPrintTimeNano < 0) {
                    elapsedPrintTimeSec = shmData->clock.seconds - lastPrintTimeSec;
                    elapsedPrintTimeNano += SECOND;
                }
                elapsedTotalTimeNano = (elapsedPrintTimeSec * SECOND) + elapsedPrintTimeNano;
                if (elapsedTotalTimeNano >= 1000000000) {
                    outputProcessTable();
                    outputFrameTable(file);
                    lastPrintTimeSec = shmData->clock.seconds;
                    lastPrintTimeNano = shmData->clock.nano;
                }

                // LAUNCH NEW CHILD IF WITHIN BOUNDS OF SPECIFICATIONS
                activeChildren = countActiveChildren();
                // CHECK IF STILL CHILDREN TO LAUNCH
                if (launchedChildren < proc) {
                    // CHECK IF MAX ACTIVE CHILDREN ALREADY MET
                    if (activeChildren < simul) {
                        // CHECK IF REQUIRED TIME HAS PASSED TO LAUNCH NEW CHILD
                        if (timeToLaunchProcess(lastLaunchSec, lastLaunchNano, intervalInMsToLaunchChildren)) {
                            // CHECK IF OPEN SPOT IN PROCESS TABLE
                            openIndex = openSpaceInTable();
                            if (openIndex >= 0) {
                                //fork new child
                                launchedPid = fork();
                                if (launchedPid < 0) {
                                    cerr << "ERROR: Fork failed." << endl;
                                    exit(1);
                                }
                                //inside child
                                else if (launchedPid == 0) {
                                    //exec worker - replace child
                                    execlp("./worker", "worker", (char*)nullptr);
                                    //check for failed exec
                                    cerr << "ERROR: Exec failed." << endl;
                                    exit(1);
                                } else { //inside oss
                                    //add new process info to process table
                                    processTable[openIndex].occupied = 1;
                                    processTable[openIndex].pid = launchedPid;
                                    processTable[openIndex].startSeconds = shmData->clock.seconds;
                                    processTable[openIndex].startNano = shmData->clock.nano;
                                    processTable[openIndex].messagesSent = 0;
                                    for (int i = 0; i < PAGE_COUNT; i++) {
                                        processTable[openIndex].pageTable[i] = -1;
                                    }
                                    //initialize page tables
                                    for (int i = 0; i < PAGE_COUNT; ++i) {
                                        processTable[openIndex].pageTable[i] = -1;
                                    }
                                    //increment launchedChildren
                                    launchedChildren++;
                                    //update last child launch time
                                    lastLaunchSec = shmData->clock.seconds;
                                    lastLaunchNano = shmData->clock.nano;
                                    outputProcessTable();
                                }
                            }
                        }
                    }
                }
        }

        // CLEAN UP SHARED MEMORY
        shmdt(shmData);
        shmctl(shmid, IPC_RMID, NULL);
        // CLEAN UP MESSAGE QUEUE
        if (msgctl(msqid, IPC_RMID, NULL) == -1) {
            cerr << "ERROR: msgctl failed in oss \n";
            exit(1);
        }

        // OUTPUT SUMMARY
        cout << "PROGRAM SUMMARY" << endl;
        cout << "Processes Launched: " << proc << endl;
        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
            file << "PROGRAM SUMMARY" << endl;
            logLinesWritten++;
            file << "Processes Launched: " << proc << endl;
            logLinesWritten++;
            file << "Messages Sent: " << totalMessagesSent << endl;
            logLinesWritten++;
        }
        float accessRate = (float)totalMemoryAccesses / (time(NULL) - startWallTime);
        float faultRate = (totalMemoryAccesses > 0) ? (float)totalPageFaults / totalMemoryAccesses : 0.0f;
        cout << "\n===PROGRAM SUMMARY===\n";
        cout << "Processes Launched: " << launchedChildren << endl;
        cout << "Memory Accesses: " << totalMemoryAccesses << endl;
        cout << "Page Faults: " << totalPageFaults << endl;
        cout << "Memory Access Rates: " << accessRate << " accesses/sec" << endl;
        cout << "Page Fault Rate: " << faultRate << " faults/access" << endl;
        if (verbose && (logLinesWritten < MAX_LOG_LINES)) {
            file << "\n===PROGRAM SUMMARY===\n";
            file << "Processes Launched: " << launchedChildren << endl;
            file << "Memory Accesses: " << totalMemoryAccesses << endl;
            file << "Page Faults: " << totalPageFaults << endl;
            file << "Memory Access Rates: " << accessRate << " accesses/sec" << endl;
            file << "Page Fault Rate: " << faultRate << " faults/access" << endl;
            logLinesWritten += 6;
        }


        // CLOSE LOG FILE
        file.close();

        return 0;
}

// FUNCTION TO PRINT HELP MESSAGE/USAGE
void printUsage() {
    cout << "Usage: oss [-h] [-n proc] [-s simul] [-i intervalInMsToLaunchChildren]\n";
    cout << " -h                               -> Display help message\n";
    cout << " -n proc                          -> Total processes to launch\n";
    cout << " -s simul                         -> Number of processes that can run simultaneously\n";
    cout << " -i intervalInMsToLaunchChildren   -> Interval (in ms) to launch children\n";
    cout << " -f logfile                       -> Name of file in which to write output\n";
}

// FUNCTION TO INCREMENT CLOCK
void incrementClock() {
    int active = countActiveChildren();
    int msToIncrement = 250;
    if (active > 0) {
        msToIncrement = 250 / active;
        shmData->clock.nano += (msToIncrement * 1000000);
    }
    else {
        shmData->clock.nano += (msToIncrement * 1000000);
    }
    if (shmData->clock.nano >= SECOND) {
        shmData->clock.seconds++;
        shmData->clock.nano = shmData->clock.nano - SECOND;
    }
}

// FUNCTION TO CLEAR PCB OF TERMINATED CHILD IN PROCESS TABLE
void updatePCBofTerminatedChild(pid_t termPid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processTable[i].occupied == 1 && processTable[i].pid == termPid) {
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].messagesSent = 0;
            for (int j = 0; j < PAGE_COUNT; j++) {
                processTable[i].pageTable[j] = -1;
            }
        }
    }
}

// FUNCTION TO OUTPUT THE PROCESS TABLE
void outputProcessTable() {
    cout << "OSS PID: " << getpid() << " SysClockS: " << shmData->clock.seconds << " SysClockNano: " << shmData->clock.nano << endl;
    cout << "Process Table:" << endl;
    cout << setw(5) << "Entry";
    cout << setw(10) << "Occupied";
    cout << setw(10) << "PID";
    cout << setw(10) << "StartS";
    cout << setw(10) << "StartN";
    cout << setw(10) << "Msgs Sent" << endl;
    for (int i = 0; i < 20; i++) {
        cout << setw(5) << i;
        cout << setw(10) << processTable[i].occupied;
        cout << setw(10) << processTable[i].pid;
        cout << setw(10) << processTable[i].startSeconds;
        cout << setw(10) << processTable[i].startNano;
        cout << setw(10) << processTable[i].messagesSent;
        cout << endl;
    }
}

// FUNCTION TO HANDLE TIMEOUT SIGNAL
void signal_handler(int sig) {
    //send kill command to all children based on pids in proc table
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 1 && processTable[i].pid > 0) {
            if (kill(processTable[i].pid, SIGTERM) == 0) {
                cout << "Successfully killed process " << processTable[i].pid << endl;
            }
            else {
                cerr << "ERROR: unable to kill process " << processTable[i].pid << endl;
            }
        }
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
    }

    cout << "Active children at alarm time: " << countActiveChildren() << endl;

    //free shm and message queue then exit
    msgctl(msqid, IPC_RMID, NULL);
    shmdt(shmData);
    shmctl(shmid, IPC_RMID, NULL);
    exit(1);
}

// FUNCTION TO COUNT THE ACTIVE CHILDREN IN THE SYSTEM
int countActiveChildren() {
    int activeChildCount = 0;
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 1) {
            activeChildCount++;
        }
    }
    return activeChildCount;
}

// FUNCTION TO CHECK IF THERE IS AN OPEN SPOT IN THE PROCESS TABLE
int openSpaceInTable() {
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 0) {
            return i;
        }
    }
    return -1;
}

// FUNCTION TO CHECK IF IT IS TIME TO LAUNCH A NEW PROCESS
bool timeToLaunchProcess(int lastChildLaunchTimeSec, int lastChildLaunchTimeNano, int launchInterval) {
    int lastChildLaunchTimeMs = (lastChildLaunchTimeSec * 1000) + (lastChildLaunchTimeNano / 1000000);
    int currentTimeMs = (shmData->clock.seconds * 1000) + (shmData->clock.nano / 1000000);
    return ((currentTimeMs - lastChildLaunchTimeMs) >= launchInterval);
}

// FUNCTION TO FIND A SPECIFIC PROCESS IN THE PROCESS TABLE
int findProcessIndex(pid_t pid) {
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied && processTable[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

// FUNCTION TO OUTPUT THE FRAME TABLE INFORMATION
void outputFrameTable(ofstream& file) {
    cout << "\nCurrent memory layout at time " << shmData->clock.seconds << ":" << shmData->clock.nano << " is:\n";
    cout << setw(8) << "Frame" << setw(10) << "PID" << setw(10) << "Page" << setw(10) << "Dirty" << setw(15) << "LastRefS" << setw(15) << "LastRefNano" << endl;

    file << "\nCurrent memory layout at time " << shmData->clock.seconds << ":" << shmData->clock.nano << " is:\n";
    file << setw(8) << "Frame" << setw(10) << "PID" << setw(10) << "Page" << setw(10) << "Dirty" << setw(15) << "LastRefS" << setw(15) << "LastRefNano" << endl;
    logLinesWritten += 2;
    for (int i = 0; i < FRAME_COUNT; ++i) {
        if (frames[i].occupied) {
            cout << setw(8) << i << setw(10) << frames[i].pid << setw(10) << frames[i].pageNumber << setw(10) << frames[i].dirty << setw(15) << frames[i].lastRefSec << setw(15) << frames[i].lastRefNano << endl;
            file << setw(8) << i << setw(10) << frames[i].pid << setw(10) << frames[i].pageNumber << setw(10) << frames[i].dirty << setw(15) << frames[i].lastRefSec << setw(15) << frames[i].lastRefNano << endl;
        }
        else {
            cout << setw(8) << i << " (empty)\n";
            file << setw(8) << i << " (empty)\n";
        }
    }
    cout << "\nPage Tables: ";
    file << "\nPage Tables: ";
    logLinesWritten++;

    for (int i = 0; i < MAX_PROCS; ++i) {
        if (processTable[i].occupied) {
            cout << "P" << i << ": [ ";
            file << "P" << i << ": [ ";
            for (int j = 0; j < PAGE_COUNT; ++j) {
                cout << processTable[i].pageTable[j] << " ";
                file << processTable[i].pageTable[j] << " ";
            }
            cout << "]\n";
            file << "]\n";
        }
    }
}
