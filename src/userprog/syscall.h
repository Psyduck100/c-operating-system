
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) - 1)

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) - 1)

extern struct lock filesys_lock;

void syscall_init (void);
void exit (int status);
void munmap (mapid_t mapid);
#endif /* userprog/syscall.h */
