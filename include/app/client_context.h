#ifndef CLIENT_CONTEXT_H
#define CLIENT_CONTEXT_H

#include <sys/types.h>

typedef enum {
    
}ClientState;

typedef struct {
    ClientState state;
} ClientContext;

#endif