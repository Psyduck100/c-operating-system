#include "userprog/exception.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_STACK_SIZE 8 * 1024 * 1024 // 8 mb

/* Number of page faults processed. */
static long long page_fault_cnt;

extern bool install_page (void *upage, void *kpage, bool writable);
static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

struct supp_page_table_entry *expand_stack (void *fault_addr);
bool vm_page_fault_helper (struct supp_page_table_entry *spte);
/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void)
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void)
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f)
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n", thread_name (),
              f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit ();

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel");

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n", f->vec_no,
              intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/*returns true if the fault addr is a result of stack overflow and meets valid
cond for stack growth*/
static bool
valid_stack_growth (void *fault_addr, void *esp)
{
  if (fault_addr >= PHYS_BASE)
    return false;
  if (fault_addr < esp - 32)
    return false;
  if ((size_t)(PHYS_BASE - pg_round_down (fault_addr)) > MAX_STACK_SIZE)
    return false;
  return true;
}

/* Helper for page fault to deal with cases involving
   the vm project 3. (list cases later when working)
   Returns true if successfully done and false otherwise*/
bool
vm_page_fault_helper (struct supp_page_table_entry *spte)
{
  /*if the supp_page_table_entry spte already has a physical frame
    then we don't do anything*/
  if (spte->in_memory)
    {
      return true;
    }

  if (spte->type == VM_BIN || spte->type == VM_FILE)
    {

      /*page allocation for physical memory*/
      void *kpage = frame_allocate (spte, PAL_USER);

      /*if no physical frame/page left and frame_allocate did not properly
      swap out frames to free up space*/
      if (kpage == NULL)
        {

          return false;
        }

      /*pin the frame so that another process can't evict the frame we are
      still loading in*/
      if (spte->frame != NULL)
        {
          spte->frame->pinned = true;
        }

      bool success = load_file (spte, kpage);

      if (!success)
        {

          frame_free (kpage);
          return false;
        }

      /*Create mapping between user virtual address to the kernel virtual
        address (physical frame)*/
      success = install_page (spte->vaddr, kpage, spte->writable);

      if (!success)
        {

          frame_free (kpage);
          return false;
        }

      spte->in_memory = true;

      /*unpin the frame after it is done loading and installed*/
      if (spte->frame != NULL)
        {
          spte->frame->pinned = false;
        }

      return true;
    }

  // if ANON just swap in and evict - all handled via frame_allocate
  if (spte->type == VM_ANON)
    {
      void *kpage = frame_allocate (spte, PAL_USER | PAL_ZERO);

      if (kpage == NULL)
        return false;

      /*pin the frame so that another process can't evict the frame we are
      still loading in*/
      if (spte->frame != NULL)
        {
          spte->frame->pinned = true;
        }

      /* If page was swapped out */
      if (spte->swap_slot != -1)
        {
          swap_in (spte->swap_slot, kpage);
          spte->swap_slot = -1;
        }
      /* Else new page with 0's */
      else
        {
          memset (kpage, 0, PGSIZE);
        }

      bool success = install_page (spte->vaddr, kpage, spte->writable);

      if (!success)
        {
          frame_free (kpage);
          return false;
        }

      spte->in_memory = true;

      /*unpin the frame after it is done loading and installed*/
      if (spte->frame != NULL)
        {
          spte->frame->pinned = false;
        }

      return true;
    }

  return false;
}

struct supp_page_table_entry *
expand_stack (void *fault_addr)
{
  void *v_page_addr = pg_round_down (fault_addr);
  return create_anon_spte (v_page_addr);
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
*/
static void
page_fault (struct intr_frame *f)
{
  bool not_present;
  bool write;
  bool user;
  void *fault_addr;

  asm ("movl %%cr2, %0" : "=r"(fault_addr));
  intr_enable ();
  page_fault_cnt++;

  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /*for project 3 if the virtual address does not have a physical
      mapping (not_present == true) then we call the handler to deal
      with the cases (i.e swapping, mmap, load the file from disk etc.)*/
  if (not_present)
    {
      struct supp_page_table_entry *spte = find_spte (fault_addr);
      if (spte == NULL)
        {
          void *esp = user ? f->esp : thread_current ()->user_esp;
          if (valid_stack_growth (fault_addr, esp))
            spte = expand_stack (fault_addr);
        }

      if (spte != NULL)
        {
          if (vm_page_fault_helper (spte))
            return; /* page successfully loaded */
        }
    }

  /*For method 2 of making sure invalid pointers must be rejected
    without harm to the kernel or other running processes,*/
  if (!user && fault_addr < PHYS_BASE)
    {
      /*This is to make sure that we don't leak resources
        when checking for invalid pointers. After doing get_user
        or put_user if there is a fault we need to recover from
        it. We set f->eip to f->epx to basically skip the portion
        that caused the page fault so we can do things such as
        releasing locks or free memory. We set f->eax to
        0xffffffff (which is -1) so when we return from page_fault()
        and go back to get_user() or put_user() they will correctly
        return -1 and false respectively on failure. Then after
        resources are properly sorted the processes can be terminated*/
      f->eip = (void *)f->eax;
      f->eax = 0xffffffff;

      /*return so we don't kill the process before releasing locks
        and freeing memory*/
      return;
    }

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading", user ? "user" : "kernel");
  if (user)
    exit (-1);
  else
    kill (f);
}
