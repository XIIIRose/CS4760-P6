////////////////////////////////////////////////////////////////////////////////////////////
//
//      msgq.h
//
//      Created By: Autumn Rose
//
//      CMPSCI 4760 - Operating Systems
//      Project 6: Paging
//
//      Due Date: 15 May 2025
//
//      Description: This file provides the message buffer structure and permissions
//              required for message passing between oss and worker.
//
////////////////////////////////////////////////////////////////////////////////////////////

#ifndef MSGQ_H
#define MSGQ_H

//LIBRARIES

#define PERMS 0644

typedef struct msgBuffer {
    long mtype;
    pid_t pid;
    int address;
    int action;
    int status;
} msgBuffer;

#endif //MSGQ_H
