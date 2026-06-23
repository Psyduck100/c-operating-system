#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler(struct intr_frame *);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int get_user(const uint8_t *uaddr) {
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool put_user(uint8_t *udst, uint8_t byte) {
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

/*Copies size bytes from usrc into dst. Makes sure to check if any
  pointers are invalid and if so ........*/
static void copy_in(void *dst_, const void *usrc_, size_t size) {

  /*set pointers to uint8_t becasue get_user reads 1 byte at a time*/
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  /*checks if usrc (user pointer) is null or points below PHYS_BASE*/
  if (usrc == NULL || usrc >= PHYS_BASE){
    /*idk what to put here*/
  }

  /*byte by byte copies usrc to dst using get_user*/
  for (int i = 0; i < size; i ++){
    uint8_t byte_value = get_user (usrc);

    /*checks if get_user had a segfault*/
    if (byte_value == -1 ){
      /*add more*/
    }

    *dst = byte_value;
    dst++;
    usrc++;
  }
}

static void syscall_handler(struct intr_frame *f UNUSED) {
  uint32_t syscall_number;

  thread_exit();
}
