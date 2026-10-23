
#ifndef BANKERS_H
#define BANKERS_H

#include "state.h"
#include "results.h"


#define BANKERS_MSG_GRANTED  "Request granted: the system stays in a SAFE state"
#define BANKERS_MSG_EXCEEDED "Error: process exceeded its maximum claim"
#define BANKERS_MSG_WAIT     "Process must wait: resources not available"
#define BANKERS_MSG_UNSAFE   "Request denied: would lead to an UNSAFE state"


typedef enum {
    BANKERS_GRANTED, /* green  */
    BANKERS_WAIT,    /* orange: not enough Available right now */
    BANKERS_DENIED,  /* orange: would lead to an unsafe state  */
    BANKERS_ERROR    /* red:    invalid request                */
} BankersOutcome;


SafetyResult bankers_is_safe(const SystemState *s);


RequestResult bankers_request(SystemState *s, int pid, const int request[MAX_RES]);


BankersOutcome bankers_outcome(const RequestResult *r);

#endif 
