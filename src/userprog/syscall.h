
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"


void syscall_init (void);
static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
static bool copy_in(void *dst, const void *usrc, size_t size);
static void halt (void);
extern struct lock filesys_lock;
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static bool open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
#endif /* userprog/syscall.h */
