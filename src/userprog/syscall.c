#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
static int get_user(const uint8_t *uaddr);
//static bool put_user(uint8_t *udst, uint8_t byte);
static bool copy_in(void *dst, const void *usrc, size_t size);
static void halt (void);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
static void exit (int status);
static pid_t exec (const char *cmdline);
static int wait (pid_t pid);

struct lock filesys_lock;

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
// static bool 
// put_user (uint8_t *udst, uint8_t byte)
// {
//   int error_code;
//   asm ("movl $1f, %0; movb %b2, %1; 1:"
//        : "=&a"(error_code), "=m"(*udst)
//        : "q"(byte));
//   return error_code != -1;
// }

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

/* sets byte to the user address of udst. Returns true if successful
and false if an error occured such as an invalid pointer*/
// static bool
// set_value(uint8_t *udst, uint8_t byte) {
//   /*checks if udst (user pointer) is null or points below PHYS_BASE*/
//   if (udst == NULL || udst >= (uint8_t *)PHYS_BASE)
//     {
//       return false;
//     }
  
//   bool success = put_user(udst, byte);
//    /*checks if put_user had a segfault*/
//   if (success == false)
//     {
//       return false;
//     }
  
//   return true;
// }

/*checks if the file pointer is valid. Returns false if invalid
  and true otherwise*/
static bool 
check_file_pointer(const char *file){
  /*checks if file pointer is null or points below PHYS_BASE*/
  if (file == NULL || file >= (const char *)PHYS_BASE)
    {
      return false;
    }
  
  int success = get_user((const uint8_t *)file);
   /*checks if put_user had a segfault*/
  if (success == -1)
    {
      return false;
    }
  
  return true;
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
  if (usrc == NULL || usrc >= (uint8_t *)PHYS_BASE)
    {
      return false;
    }

  /*byte by byte copies usrc to dst using get_user*/
  for (size_t i = 0; i < size; i++)
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
      thread_exit();
    }

  switch (syscall_number)
    {
    case SYS_HALT:
      /*call halt*/
      halt();
      break;

    case SYS_EXIT:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      /*call exit*/
      exit((int)args[0]);
      break;

    case SYS_EXEC:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }
      
      /* fix resources and terminate if invalid pointer*/
      if (check_file_pointer(*(char **)args[0]) == false){
        thread_exit();
      }

      /*exec call*/
      f->eax = exec((char*)args[0]);

      break;

    case SYS_WAIT:
      /*wait call*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      /*exec call*/
      f->eax = wait((pid_t)args[0]);


      break;
    case SYS_CREATE:
      
      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }
      
      if (check_file_pointer(*(char **)args[0]) == false){
        /* fix resources and terminate if invalid pointer*/
        thread_exit();
      }

      f->eax = create(*(char **)args[0], *(unsigned *)args[1]);
      break;

    case SYS_REMOVE:
      
      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      /* fix resources and terminate if invalid pointer*/
      if (check_file_pointer(*(char **)args[0]) == false){
        thread_exit();
      }

      f->eax = remove(*(char **)args[0]);
      break;

    case SYS_OPEN:
      
      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }
      
      /* fix resources and terminate if invalid pointer*/
      if (check_file_pointer(*(char **)args[0]) == false){
        thread_exit();
      }

      f->eax = open(*(char **)args[0]);
      break;

    case SYS_FILESIZE:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      f->eax = filesize(*(int *)args[0]);
      break;

    case SYS_READ:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      /* fix resources and terminate if invalid pointer*/
      if (check_file_pointer(*(char **)args[1]) == false){
        thread_exit();
      }
      f->eax = read(*(int *)args[0], *(void **)args[1], *(unsigned *)args[2]);
      break;

    case SYS_WRITE:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      /* fix resources and terminate if invalid pointer*/
      if (check_file_pointer(*(char **)args[1]) == false){
        thread_exit();
      }

      f->eax = write(*(int *)args[0], *(void **)args[1], *(unsigned *)args[2]);
      break;

    case SYS_SEEK:
      
      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      seek(*(int *)args[0], *(unsigned *)args[1]);
      break;

    case SYS_TELL:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      f->eax = tell(*(int *)args[0]);
      break;

    case SYS_CLOSE:

      /*get arguments*/
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 1);

      /* fix resources and terminate if invalid pointer*/
      if (success == false)
      {
        thread_exit();
      }

      close(*(int *)args[0]);
      break;

    default:
      thread_exit ();
      break;
    }
}

/*Stops the entire operating system*/
static void
halt (void)
{
  shutdown_power_off();
}

/*Prints an exit message with the processes name and the exit status
then exits the thread*/
static void
exit (int status){
  struct thread *t = thread_current();
  t->exit_status = status;


  /*prints exit  message*/
  printf("%s: exit(%d)\n", thread_current()->name, status);
  sema_up(&t->wait_sema);
  sema_down(&t->die_sema);
  thread_exit();
}

/* Creates and executes a new child process from cmdline. The parent
 process will wait until the child process is finished loading 
 before continuing. Returns tid of child process on success and
 -1 on failure*/
static pid_t 
exec(const char *cmdline){
  tid_t tid = process_execute(cmdline);

  if (tid == TID_ERROR){
    return -1;
  }

  return tid;
}

static int
wait(pid_t pid){
  return process_wait(pid);
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

/* Opens FILE, adds it to the current thread's file descriptor table, and
 * returns its file descriptor. Returns -1 if the file coudldn't be opened or
 * the fd table is full*/
static int
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

  for(int i = 2; i < 64; i++) {
    if(t->fd_table[i] == NULL) {
      t->fd_table[i] = f;
      return i;
    }
  }

  t->fd_table[t->next_fd] = f;
  return t->next_fd++;
}

/* Reads SIZE bytes from the file at FD into BUFFER, or reading from stdin if
 * FD is 0, and returns the number or bytes read or -1 on error*/

static int
filesize (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  int size = file_length (f);
  lock_release (&filesys_lock);
  return size;
}

/* Reads from a file and writes to a buffer. Returns the number of bytes
 * read. */

static int
read (int fd, void *buffer, unsigned size)
{
  if (buffer == NULL)
    return -1;

  if (fd == 0)
    {
      /* Type cast into uint8_t because input_getc returns a uint8_t */
      uint8_t *buf = buffer;
      /* Read from standard input */
      for (unsigned i = 0; i < size; i++)
        {
          buf[i] = input_getc ();
        }
      return size;
    }

  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  int bytes_read = file_read (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER to either stdout (if FD is 1) or a file,
 * returning the number of bytes written*/

static int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
    {
      /* Write to stdout since fd is 1 */
      putbuf (buffer, size);
      return size;
    }

  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  int bytes_written = file_write (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes_written;
}

/* Moves the read/write position of the file at FD to POSITION bytes from the
 * start of the file*/

static void
seek (int fd, unsigned position)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  file_seek (f, position);
  lock_release (&filesys_lock);
}

/* Returns the current read/write position of the file at FD */

static unsigned
tell (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  unsigned position = file_tell (f);
  lock_release (&filesys_lock);
  return position;
}

/* Closes the file at FD and sets its entry in the fd table to NULL so the fd
 * can't be used again */

static void
close (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  file_close (f);
  lock_release (&filesys_lock);
  thread_current ()->fd_table[fd] = NULL;
}