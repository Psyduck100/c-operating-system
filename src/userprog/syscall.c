#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>

#define READDIR_MAX_LEN 14

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static bool copy_in (void *dst, const void *usrc, size_t size);
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
static pid_t exec (const char *cmdline);
static int wait (pid_t pid);
static bool chdir (const char *dir);
static bool mkdir (const char *dir);
static bool readdir (int fd, char name[READDIR_MAX_LEN + 1]);
static bool isdir (int fd);
static int inumber (int fd);

void
syscall_init (void)
{
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

/* Returns true if file is a directory, false if it's a regular file */
static bool
file_is_directory (struct file *file)
{
  if (file == NULL)
    {
      return false;
    }
  struct inode *inode = file_get_inode (file);
  return inode->data.file_or_dir == 1;
}

/*checks if a pointer is valid. Returns false if invalid
  and true otherwise*/
static bool
check_pointer (const uint8_t *pointer)
{
  /*checks if pointer is null or points below PHYS_BASE*/
  if (pointer == NULL || pointer >= (const uint8_t *)PHYS_BASE)
    {
      return false;
    }

  int success = get_user ((const uint8_t *)pointer);
  /*checks if get_user had a segfault*/
  if (success == -1)
    {
      return false;
    }

  return true;
}

/*checks if a buffer is valid. Returns false if invalid
  and true otherwise*/
static bool
check_buffer (const uint8_t *buffer, size_t size)
{

  const uint8_t *ptr = buffer;

  if (buffer == NULL)
    {
      return false;
    }

  for (size_t i = 0; i < size; i++)
    {
      bool valid = check_pointer (ptr + i);
      if (valid == false)
        {
          return false;
        }
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

  /*byte by byte copies usrc to dst using get_user*/
  for (size_t i = 0; i < size; i++)
    {

      /*checks if usrc (user pointer) is null or points below PHYS_BASE*/
      if (usrc == NULL || usrc >= (uint8_t *)PHYS_BASE)
        {
          return false;
        }

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
      exit (-1);
    }

  switch (syscall_number)
    {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      exit (args[0]);
      break;

    case SYS_EXEC:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = exec ((char *)args[0]);
      break;

    case SYS_WAIT:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      f->eax = wait ((pid_t)args[0]);
      break;

    case SYS_CREATE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = create ((char *)args[0], (unsigned)args[1]);
      break;

    case SYS_REMOVE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = remove ((char *)args[0]);
      break;

    case SYS_OPEN:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = open ((char *)args[0]);
      break;

    case SYS_FILESIZE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      f->eax = filesize (args[0]);
      break;

    case SYS_READ:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);
      if (!success)
        exit (-1);
      if (!check_buffer ((uint8_t *)args[1], (size_t)args[2]))
        exit (-1);
      f->eax = read (args[0], (void *)args[1], (unsigned)args[2]);
      break;

    case SYS_WRITE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);
      if (!success)
        exit (-1);
      if (!check_buffer ((uint8_t *)args[1], (size_t)args[2]))
        exit (-1);
      f->eax = write (args[0], (void *)args[1], (unsigned)args[2]);
      break;

    case SYS_SEEK:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);
      if (!success)
        exit (-1);
      seek (args[0], (unsigned)args[1]);
      break;

    case SYS_TELL:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      f->eax = tell (args[0]);
      break;

    case SYS_CLOSE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      close (args[0]);
      break;

    case SYS_CHDIR:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = chdir ((char *)args[0]);
      break;

    case SYS_MKDIR:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[0]))
        exit (-1);
      f->eax = mkdir ((char *)args[0]);
      break;

    case SYS_READDIR:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);
      if (!success)
        exit (-1);
      if (!check_pointer ((uint8_t *)args[1]))
        exit (-1);
      f->eax = readdir (args[0], (char *)args[1]);
      break;

    case SYS_ISDIR:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      f->eax = isdir (args[0]);
      break;

    case SYS_INUMBER:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      f->eax = inumber (args[0]);
      break;

    default:
      exit (-1);
      break;
    }
}

/*Stops the entire operating system*/
static void
halt (void)
{
  shutdown_power_off ();
}

/*Prints an exit message with the processes name and the exit status
then exits the thread*/
void
exit (int status)
{
  struct thread *t = thread_current ();
  t->exit_status = status;

  /* Our die_sema makes it so that child threads don't call thread_exit()
      until the parent thread is exited. However, we still need to treat
      this current child thread as dead and as such need to make its file
      writable again*/
  if (t->running_file != NULL)
    {
      struct file *f = t->running_file;
      t->running_file = NULL;
      file_close (f);
    }

  // prints exit message
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

/* Creates and executes a new child process from cmdline. The parent
 process will wait until the child process is finished loading
 before continuing. Returns tid of child process on success and
 -1 on failure*/
static pid_t
exec (const char *cmdline)
{
  tid_t tid = process_execute (cmdline);

  if (tid == TID_ERROR)
    {
      return -1;
    }

  return tid;
}

static int
wait (pid_t pid)
{
  return process_wait (pid);
}

/* Creates a new file named FILE initially SIZE bytes in size. Returns true if
 * successful, false otherwise. */
static bool
create (const char *file, unsigned initial_size)
{
  if (file == NULL)
    return false;

  bool result = filesys_create (file, initial_size);

  return result;
}

/* Removes the file named FILE. Returns true if successful, false otherwise. */
static bool
remove (const char *file)
{
  if (file == NULL)
    return false;

  bool result = filesys_remove (file);

  return result;
}

/* Opens FILE, adds it to the current thread's file descriptor table, and
 * returns its file descriptor. Returns -1 if the file coudldn't be opened or
 * the fd table is full*/
static int
open (const char *file)
{
  if (file == NULL)
    return -1;

  struct file *f = filesys_open (file);

  if (f == NULL)
    return -1;

  struct thread *t = thread_current ();

  for (int i = 2; i < 64; i++)
    {
      if (t->fd_table[i] == NULL)
        {
          t->fd_table[i] = f;
          return i;
        }
    }

  file_close (f);
  return -1;
}

/* Reads SIZE bytes from the file at FD into BUFFER, or reading from stdin if
 * FD is 0, and returns the number or bytes read or -1 on error*/

static int
filesize (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  int size = file_length (f);
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
  if (f == NULL || file_is_directory (f))
    return -1;

  int bytes_read = file_read (f, buffer, size);
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
  if (f == NULL || file_is_directory (f))
    return -1;

  int bytes_written = file_write (f, buffer, size);
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

  file_seek (f, position);
}

/* Returns the current read/write position of the file at FD */

static unsigned
tell (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  unsigned position = file_tell (f);

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

  thread_current ()->fd_table[fd] = NULL;

  file_close (f);
}

/* Return true if fd is a directory, false if fd is an ordinary file */
static bool
isdir (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return false;

  return file_is_directory (f);
}

/* Returns the inode number of the inode associated with fd */
static int
inumber (int fd)
{
  struct file *f = get_file (fd);
  if (f == NULL)
    return -1;

  struct inode *inode = file_get_inode (f);
  return inode->sector;
}

/* Creates the directory DIR. Returns true if successful, false if not. */
static bool
mkdir (const char *dir)
{
  return filesys_dir_create (dir);
}

/* Changes the current woring directory to dir.
   Returns true if successful, false if not. */
static bool
chdir (const char *dir)
{
  struct dir *parent = NULL;
  char *entry_name = traverse_path (dir, &parent);

  if (parent == NULL)
    {
      return false;
    }

  struct dir *target = NULL;

  /* If there is a final component to the arg */
  if (entry_name != NULL)
    {
      struct inode *inode = NULL;
      if (!dir_lookup (parent, entry_name, &inode))
        {
          dir_close (parent);
          return false;
        }

      dir_close (parent);
      target = dir_open (inode);

      if (target == NULL)
        {
          inode_close (inode);
          return false;
        }
    }

  /* No final component to arg */
  else
    {
      target = parent;
    }

  /* Replace current working directory */
  struct thread *cur = thread_current ();
  dir_close (cur->cur_dir);
  cur->cur_dir = target;

  return true;
}

/* Reads a directory entry from file descriptor fd which must represent a
   directory If successful store file name in name and return true*/
static bool
readdir (int fd, char name[READDIR_MAX_LEN + 1])
{
  struct file *f = get_file (fd);
  if (f == NULL || !file_is_directory (f))
    {
      return false;
    }

  struct inode *inode = inode_reopen (f->inode);

  struct dir *cur_dir = dir_open (inode);
  cur_dir->pos = f->pos;

  bool success = dir_readdir (cur_dir, name);

  f->pos = cur_dir->pos;

  dir_close (cur_dir);

  return success;
}