
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

extern struct lock filesys_lock;

void syscall_init (void);
#endif /* userprog/syscall.h */
