#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>

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
mapid_t mmap (int fd, void *addr);
void munmap (mapid_t mapid);
static bool check_buffer_writable (const uint8_t *buffer, size_t size);
static bool set_pin_page (const void *vaddr, bool pinned_value);
static bool set_pin_buffer (const uint8_t *buffer, size_t size,
                            bool pinned_value);

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

/*checks if the pages for a buffer is writable. Returns true if all
pages in buffer are writable and false otherwise. Created to make
sure file_read does not page fault while holding the filesys lock*/
static bool
check_buffer_writable (const uint8_t *buffer, size_t size)
{

  const uint8_t *ptr = buffer;

  if (buffer == NULL)
    {
      return false;
    }
  if (size == 0)
    {
      return true;
    }

  while (ptr < buffer + size)
    {
      struct supp_page_table_entry *spte = find_spte (ptr);
      if (spte == NULL || spte->writable == false)
        return false;

      ptr += PGSIZE;
    }

  return true;
}

/*sets the value of pinned of the physical page/frame related to the page at
virtual address vaddr to pinned_value. Returns true on success and false on
failure.*/
static bool
set_pin_page (const void *vaddr, bool pinned_value)
{
  struct supp_page_table_entry *spte = find_spte (vaddr);

  if (spte == NULL)
    {
      return false;
    }

  if (!spte->in_memory)
    {
      /*if not in memory use our already created page fault helper to bring it
      back to memory. Basically demand paging.*/
      bool success = vm_page_fault_helper (spte);
      if (!success)
        {
          return false;
        }
    }

  spte->frame->pinned = pinned_value;

  return true;
}

/*sets pinned argument to pinned_value for all pages for the buffer for read
and write system calls. Returns true on success and false on failure*/
static bool
set_pin_buffer (const uint8_t *buffer, size_t size, bool pinned_value)
{
  const uint8_t *cur_addr = buffer;

  if (size == 0)
    {
      return true;
    }

  /* loop through all pages/frames and sets all pinned arguments for each page
  to pinned_value*/
  while (cur_addr <= (buffer + size - 1))
    {
      bool success = set_pin_page (cur_addr, pinned_value);
      if (!success)
        return false;
      cur_addr += PGSIZE;
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
  /*save user stack pointer*/
  thread_current ()->user_esp = f->esp;

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
      if (!check_buffer_writable ((uint8_t *)args[1], (size_t)args[2]))
        exit (-1);
      if (!set_pin_buffer ((uint8_t *)args[1], (size_t)args[2], true))
        exit (-1);
      f->eax = read (args[0], (void *)args[1], (unsigned)args[2]);
      if (!set_pin_buffer ((uint8_t *)args[1], (size_t)args[2], false))
        exit (-1);
      break;

    case SYS_WRITE:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 3);
      if (!success)
        exit (-1);
      if (!check_buffer ((uint8_t *)args[1], (size_t)args[2]))
        exit (-1);
      if (!set_pin_buffer ((uint8_t *)args[1], (size_t)args[2], true))
        exit (-1);
      f->eax = write (args[0], (void *)args[1], (unsigned)args[2]);
      if (!set_pin_buffer ((uint8_t *)args[1], (size_t)args[2], false))
        exit (-1);
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

    case SYS_MMAP:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args * 2);
      if (!success)
        exit (-1);
      f->eax = mmap (args[0], (void *)args[1]);
      break;

    case SYS_MUNMAP:
      success = copy_in (args, (uint32_t *)f->esp + 1, sizeof *args);
      if (!success)
        exit (-1);
      munmap ((mapid_t)args[0]);
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
      lock_acquire (&filesys_lock);
      file_close (f);
      lock_release (&filesys_lock);
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
    return -1;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);

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

  thread_current ()->fd_table[fd] = NULL;

  /* Acquire the global lock to avoid race conditions */
  lock_acquire (&filesys_lock);
  file_close (f);
  lock_release (&filesys_lock);
}

mapid_t
mmap (int fd, void *addr)
{
  // not 4 bit aligned

  if (((uintptr_t)addr) % PGSIZE != 0)
    {
      return MAP_FAILED;
    }

  /* Validate the fd */

  if (fd == 0 || fd == 1)
    {
      return MAP_FAILED;
    }

  struct thread *t = thread_current ();
  struct file *f = get_file (fd);
  if (f == NULL)
    {
      return MAP_FAILED;
    }
  /* Validate addr */

  if (addr == NULL || pg_ofs (addr) != 0)
    {
      return MAP_FAILED;
    }
  /* Acquire lock to avoid race conditions */

  lock_acquire (&filesys_lock);
  /*indep file ref so if other processes close the fd doesnt
    intefere with us*/
  struct file *f2 = file_reopen (f);
  if (f2 == NULL)
    {
      lock_release (&filesys_lock);
      return MAP_FAILED;
    }

  off_t size = file_length (f2);
  lock_release (&filesys_lock);
  if (size == 0)
    {
      lock_acquire (&filesys_lock);
      file_close (f2);
      lock_release (&filesys_lock);
      return MAP_FAILED;
    }

  // compute how many pages file needs

  int pages_needed = (size + PGSIZE - 1) / PGSIZE;
  // check the number of consecutive sptes are free

  for (int i = 0; i < pages_needed; i++)
    {
      void *v_page_addr = (uint8_t *)addr + i * PGSIZE;
      if (find_spte (v_page_addr) != NULL)
        {
          lock_acquire (&filesys_lock);
          file_close (f2);
          lock_release (&filesys_lock);
          return MAP_FAILED;
        }
    }

  // create mmap struct for tracking

  struct mmap_file *mf = malloc (sizeof (struct mmap_file));
  if (mf == NULL)
    {
      lock_acquire (&filesys_lock);
      file_close (f2);
      lock_release (&filesys_lock);
      return MAP_FAILED;
    }

  mf->mapid = t->next_mapid++;
  mf->file = f2;
  list_init (&mf->spte_list);

  /*create the spte's, 1 per page needed, keep track of the file offset
    and calculate the read and zero bytes using total file length and
    already "allocated" amt*/
  off_t offset = 0;
  for (int i = 0; i < pages_needed; i++)
    {
      void *v_page_addr = (uint8_t *)addr + i * PGSIZE;
      size_t read_bytes = (size - offset < PGSIZE) ? size - offset : PGSIZE;
      size_t zero_bytes = PGSIZE - read_bytes;

      struct supp_page_table_entry *spte = malloc (sizeof (*spte));
      if (spte == NULL)
        {
          /* undo previous SPTEs */
          while (!list_empty (&mf->spte_list))
            {
              struct list_elem *el = list_pop_front (&mf->spte_list);
              struct supp_page_table_entry *old
                  = list_entry (el, struct supp_page_table_entry, mmap_elem);
              hash_delete (&t->vm, &old->elem);
              free (old);
            }
          free (mf);
          lock_acquire (&filesys_lock);
          file_close (f2);
          lock_release (&filesys_lock);
          return MAP_FAILED;
        }

      memset (spte, 0, sizeof (*spte));

      spte->type = VM_FILE;
      spte->file = f2;
      spte->read_bytes = read_bytes;
      spte->zero_bytes = zero_bytes;
      spte->offset = offset;
      spte->vaddr = v_page_addr;
      spte->writable = true;
      spte->in_memory = false;
      spte->swap_slot = -1;

      if (hash_insert (&t->vm, &spte->elem) != NULL)
        {
          free (spte);
          /* Clean up all previously created SPTEs */
          while (!list_empty (&mf->spte_list))
            {
              struct list_elem *el = list_pop_front (&mf->spte_list);
              struct supp_page_table_entry *old
                  = list_entry (el, struct supp_page_table_entry, mmap_elem);
              hash_delete (&t->vm, &old->elem);
              free (old);
            }
          free (mf);
          lock_acquire (&filesys_lock);
          file_close (f2);
          lock_release (&filesys_lock);
          return MAP_FAILED;
        }
      list_push_back (&mf->spte_list, &spte->mmap_elem);

      offset += read_bytes;
    }

  list_push_back (&t->mmap_list, &mf->elem);
  return mf->mapid;
}
void
munmap (mapid_t mapid)
{

  struct mmap_file *mf = NULL;
  struct list_elem *e = NULL;
  // find if matching mapid exists in our thread
  for (e = list_begin (&thread_current ()->mmap_list);
       e != list_end (&thread_current ()->mmap_list); e = list_next (e))
    {
      struct mmap_file *cur = list_entry (e, struct mmap_file, elem);
      if (cur->mapid == mapid)
        {
          mf = cur;
          break;
        }
    }
  // if not return
  if (mf == NULL)
    return;

  // iterate through all the spte's belonging to that mf
  e = list_begin (&mf->spte_list);
  while (e != list_end (&mf->spte_list))
    {

      struct supp_page_table_entry *cur
          = list_entry (e, struct supp_page_table_entry, mmap_elem);

      e = list_next (e);

      // if our spte is in memory, and page is dirty, we need to write
      if (cur->in_memory == true)
        {
          if (pagedir_is_dirty (thread_current ()->pagedir, cur->vaddr))
            {

              lock_acquire (&filesys_lock);

              file_write_at (mf->file, cur->vaddr, cur->read_bytes,
                             cur->offset);
              lock_release (&filesys_lock);
            }

          void *k_page_addr
              = pagedir_get_page (thread_current ()->pagedir, cur->vaddr);
          pagedir_clear_page (thread_current ()->pagedir, cur->vaddr);
          frame_free (k_page_addr);
        }

      // free the spte and delete from thread
      hash_delete (&thread_current ()->vm, &cur->elem);
      free (cur);
    }

  // clean up mmap_file struct and open files
  lock_acquire (&filesys_lock);
  file_close (mf->file);
  lock_release (&filesys_lock);

  list_remove (&mf->elem);
  free (mf);
}
