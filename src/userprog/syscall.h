
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"


void syscall_init (void);
static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
static bool copy_in(void *dst, const void *usrc, size_t size);
extern struct lock filesys_lock;

#endif /* userprog/syscall.h */
