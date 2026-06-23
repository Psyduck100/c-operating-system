#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a"(error_code), "=m"(*udst)
       : "q"(byte));
  return error_code != -1;
}

/* Returns the current threads file at file descriptor FD */
static struct file *
get_file (int fd)
{
  struct thread *t = thread_current ();
  if (fd < 2 || fd >= 64 || t->fd_table[fd] == NULL)
    {
      return NULL;
    }
  return t->fd_table[fd];
}

/*Copies size bytes from usrc into dst. Makes sure to check if any
  pointers are invalid and if so returns false. Else returns True.*/
static bool
copy_in (void *dst_, const void *usrc_, size_t size)
{
  /*set pointers to uint8_t becasue get_user reads 1 byte at a time*/
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  /*checks if usrc (user pointer) is null or points below PHYS_BASE*/
  if (usrc == NULL || usrc >= PHYS_BASE)
    {
      return false;
    }

  /*byte by byte copies usrc to dst using get_user*/
  for (int i = 0; i < size; i++)
    {
      int byte_value = get_user (usrc);

      /*checks if get_user had a segfault*/
      if (byte_value == -1)
        {
          return false;
        }

      /*copy byte if successful*/
      *dst = byte_value;

      /*go to next byte*/
      dst++;
      usrc++;
    }

  return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t syscall_number;
  int args[3];

  /*get system call number*/
  bool success = copy_in (&syscall_number, f->esp, sizeof syscall_number);

  if (success == false)
    {
      /* fix resources and termiante*/
    }

  success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);

  if (success == false)
    {
      /* fix resources and termiante*/
    }

  switch (syscall_number)
    {
    case SYS_HALT:
      /*halt code*/
      break;
    case SYS_EXIT:
      /*exit code*/
      break;
    case SYS_EXEC:
      /*exit code*/
      break;
    case SYS_WAIT:
      /*halt code*/
      break;
    case SYS_CREATE:
      /*exit code*/
      break;
    case SYS_REMOVE:
      /*exit code*/
      break;
    case SYS_OPEN:
      /*halt code*/
      break;
    case SYS_FILESIZE:
      /*exit code*/
      break;
    case SYS_READ:
      /*exit code*/
      break;
    case SYS_WRITE:
      /*halt code*/
      break;
    case SYS_SEEK:
      /*exit code*/
      break;
    case SYS_TELL:
      /*exit code*/
      break;
    case SYS_CLOSE:
      /*exit code*/
      break;
    default:
    }

  thread_exit ();
}

/* Creates a new file named FILE initially SIZE bytes in size. Returns true if
 * successful, false otherwise. */
static bool
create (const char *file, unsigned initial_size)
{
  if (file == NULL)
    return false;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  bool result = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return result;
}

/* Removes the file named FILE. Returns true if successful, false otherwise. */
static bool
remove (const char *file)
{
  if (file == NULL)
    return false;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  bool result = filesys_remove (file);
  lock_release (&filesys_lock);
  return result;
}

/* Opens the file named FILE. Returns its file descriptor if successful, -1
 * otherwise. */
static bool
open (const char *file)
{
  if (file == NULL)
    return false;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);

  if (f == NULL)
    return -1;

  struct thread *t = thread_current ();

  if (t->next_fd >= 64)
    {
      file_close (f);
      return -1;
    }

  t->fd_table[t->next_fd] = f;
  return t->next_fd++;
}
