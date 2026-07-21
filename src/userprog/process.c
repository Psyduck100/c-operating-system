#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "vm/page.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  char *fn_copy2;
  char *save_ptr;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;

  /* Make a 2nd copy of FILE_NAME
     because strtok_r modifies the input string
     and we need to pass in the whole string into
     start_process */
  fn_copy2 = palloc_get_page (0);
  if (fn_copy2 == NULL)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }

  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (fn_copy2, file_name, PGSIZE);

  /* Tokenzie the first argument of cmd line
     instead of the whole line */
  char *prog_name = strtok_r (fn_copy, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy2);

  palloc_free_page (fn_copy);

  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy2);
      return TID_ERROR;
    }

  struct thread *cur_t = thread_current ();
  struct thread *found_child = NULL;
  struct list_elem *cur_child;

  /* find child process to sema down on its semaphore so parent process
  will wait for child to load and sema up before continuing*/
  for (cur_child = list_begin (&cur_t->child_list);
       cur_child != list_end (&cur_t->child_list);
       cur_child = list_next (cur_child))
    {
      struct thread *child_t
          = list_entry (cur_child, struct thread, child_elem);
      if (child_t->tid == tid)
        {
          found_child = child_t;
          sema_down (&found_child->load_sema);
          break;
        }
    }

  /* if child was terminated or was not loaded then set
   tid to TID_ERROR*/
  if (!found_child || found_child->loaded == false)
    {
      tid = TID_ERROR;
    }

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  char *save_ptr2;

  // Copy of file to extract filename only
  char *fn_copy = palloc_get_page (0);

  if (fn_copy == NULL)
    thread_exit ();
  strlcpy (fn_copy, file_name, PGSIZE);
  char *prog_name = strtok_r (fn_copy, " ", &save_ptr2);

  hash_init (&thread_current ()->vm, vm_hash_spte, vm_hash_spte_less_func,
             NULL);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* acquire global lock before loading file */
  lock_acquire (&filesys_lock);
  success = load (prog_name, &if_.eip, &if_.esp);
  lock_release (&filesys_lock);

  // Free copy
  palloc_free_page (fn_copy);

  /*current thread is the child thread from thread exec*/
  struct thread *cur_t = thread_current ();

  /*we set loaded to whether load was successful or not and do
  sema up so our parent process from exec can continue*/
  cur_t->loaded = success;
  sema_up (&cur_t->load_sema);

  /* If load failed, quit. */
  if (!success)
    {
      palloc_free_page (file_name);
      thread_exit ();
    }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  if (success)
    {
      // Count tokens to temp copy
      char *count_copy = palloc_get_page (0);
      if (count_copy == NULL)
        {
          palloc_free_page (file_name);
          thread_exit ();
        }
      strlcpy (count_copy, file_name, PGSIZE);

      char *saveptr_count;
      char *token;
      int argc = 0;

      char *token_count = strtok_r (count_copy, " ", &saveptr_count);
      while (token_count != NULL)
        {
          argc++;
          token_count = strtok_r (NULL, " ", &saveptr_count);
        }
      palloc_free_page (count_copy);

      // Allocate argv array and user stack address array
      char **argv = malloc (sizeof (char *) * argc);
      if (argv == NULL)
        {
          palloc_free_page (file_name);
          thread_exit ();
        }

      char **user_stack_address = malloc (sizeof (char *) * argc);
      if (user_stack_address == NULL)
        {
          free (argv);
          palloc_free_page (file_name);
          thread_exit ();
        }

      // Tokenize the command line arguments
      char *saveptr;
      int index = 0;
      token = strtok_r (file_name, " ", &saveptr);
      while (token != NULL)
        {
          argv[index] = token;
          index++;
          token = strtok_r (NULL, " ", &saveptr);
        }
      argv[argc] = NULL; // Null-terminate the argv array

      // Build User Stack: add parsed CLA
      int total_arg_chars = 0;
      for (int i = argc - 1; i >= 0; i--)
        {
          int arg_chars = strlen (argv[i]) + 1;
          total_arg_chars += arg_chars;

          if_.esp -= arg_chars;
          memcpy (if_.esp, argv[i], arg_chars);
          // save user stack address
          user_stack_address[i] = if_.esp;
        }

      // Align by 4 bytes
      int padding = (4 - (total_arg_chars % 4)) % 4;
      if_.esp -= padding;
      memset (if_.esp, 0, padding);

      // Add NULL
      if_.esp -= 4;
      *(uint32_t *)if_.esp = 0;

      // Build User Stack: add CLA item memory addresses
      for (int i = argc - 1; i >= 0; i--)
        {
          if_.esp -= 4;
          *(uint32_t *)if_.esp = (uint32_t)user_stack_address[i];
        }

      // Add user stack address of
      uint32_t argv_addr = (uint32_t)if_.esp;
      if_.esp -= 4;
      *(uint32_t *)if_.esp = argv_addr;

      // Add argc
      if_.esp -= 4;
      *(uint32_t *)if_.esp = argc;

      // Add fake return address
      if_.esp -= 4;
      *(uint32_t *)if_.esp = 0;

      // Free allocated memory
      free (argv);
      free (user_stack_address);
    }

  palloc_free_page (file_name);
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  // find child with matching tid
  struct thread *t = thread_current ();
  struct thread *child = NULL;
  struct list_elem *e;

  // iterate though list until tail (sentinel)
  for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
       e = list_next (e))
    {
      struct thread *cur = list_entry (e, struct thread, child_elem);
      if (cur->tid == child_tid)
        {
          child = cur;
          break;
        }
    }

  // handle all the -1 (fail) cases
  // element not found so child is null
  if (child == NULL)
    return -1;

  // child is already waited for
  if (child->waited_for)
    return -1;

  // child is already waited for
  child->waited_for = true;

  /* block until child exits */
  sema_down (&child->wait_sema);

  int exit_status = child->exit_status;

  /* remove child from list before letting it die */
  list_remove (&child->child_elem);

  /* allow child to die */
  sema_up (&child->die_sema);

  return exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* free all children */
  while (!list_empty (&cur->child_list))
    {
      struct list_elem *e = list_pop_front (&cur->child_list);
      struct thread *child = list_entry (e, struct thread, child_elem);

      /* Allow the child to pass its die_sema down (if it already hasn't). */
      sema_up (&child->die_sema); // unblocks child from dying

      /* Wait for the child to enter process_exit and up its wait_sema. */
      sema_down (&child->wait_sema);

      /* Now the child will continue to die, its struct remains valid
         until it actually calls thread_exit and is freed later. */
    }

  while (!list_empty (&thread_current ()->mmap_list))
    {

      struct mmap_file *cur = list_entry (
          list_begin (&thread_current ()->mmap_list), struct mmap_file, elem);
      munmap (cur->mapid);
    }
  hash_destroy (&cur->vm, vm_hash_spte_destroy_func);

  /* Close all open files  */
  if (cur->fd_table != NULL)
    {
      for (int i = 2; i < 64; i++)
        {
          if (cur->fd_table[i] != NULL)
            {
              lock_acquire (&filesys_lock);
              file_close (cur->fd_table[i]);
              cur->fd_table[i] = NULL;
              lock_release (&filesys_lock);
            }
        }
      palloc_free_page (cur->fd_table);
    }

  /* When file is done execing make it writable again*/
  if (cur->running_file != NULL)
    {
      struct file *f = cur->running_file;
      cur->running_file = NULL;
      lock_acquire (&filesys_lock);
      file_close (f);
      lock_release (&filesys_lock);
    }

  /* Destroy the current process's page directory and switch back
       to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {

      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* Notify parent and wait for permission to die */
  sema_up (&cur->wait_sema); // increments value to 1

  sema_down (&cur->die_sema); // blocks itself from dying
}
/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Deny writes to file while its running */
  file_deny_write (file);
  t->running_file = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

done:
  if (!success)
    {
      if (t->running_file != NULL)
        {
          /* file_deny_write was called, file_close handles allow internally */
          file_close (t->running_file);
          t->running_file = NULL;
        }
      else if (file != NULL)
        {
          /* file was opened but file_deny_write was never called */
          file_close (file);
        }
    }

  return success;
}

/* load() helpers. */

bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Dynamically allocate memory for supp_page_table_entry
         so that we can free when done */
      struct supp_page_table_entry *spte
          = malloc (sizeof (struct supp_page_table_entry));
      if (spte == NULL)
        return false;

      /* Initialize spte values for bin file */
      spte->type = VM_BIN;
      spte->vaddr = upage;
      spte->writable = writable;
      spte->file = file;
      spte->offset = ofs;
      spte->read_bytes = page_read_bytes;
      spte->zero_bytes = page_zero_bytes;
      spte->in_memory = false;
      spte->swap_slot = -1;

      /* Insert spte into current threads vm hash table */
      if (!hash_insert (&thread_current ()->vm, &spte->elem))
        {
          free (spte);
          return false;
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;

      /* Need to update offset for next page read
         so that each page remembers where in the
         file its own data starts */
      ofs += page_read_bytes;
    }

  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        {
          *esp = PHYS_BASE;
          /* Dynamicallly allocate memory for spte
             so that we can free it later */
          struct supp_page_table_entry *spte
              = malloc (sizeof (struct supp_page_table_entry));
          if (spte == NULL)
            {
              palloc_free_page (kpage);
              return false;
            }

          /* Initialize spte fields for anon file bc stack isn't
             backed by any file */
          spte->type = VM_ANON;
          spte->vaddr = ((uint8_t *)PHYS_BASE) - PGSIZE;
          spte->writable = true;
          spte->in_memory = true;
          spte->file = NULL;
          spte->offset = 0;
          spte->read_bytes = 0;
          spte->zero_bytes = PGSIZE;
          spte->swap_slot = -1;
          spte->frame = NULL;

          hash_insert (&thread_current ()->vm, &spte->elem);
        }
      else
        {
          palloc_free_page (kpage);
        }
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
