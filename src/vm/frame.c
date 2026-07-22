#include "filesys/file.h"
#include "frame.h"
#include "swap.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

struct list frame_table;
struct lock frame_table_lock;

void
frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

void *
frame_allocate (struct supp_page_table_entry *spte, enum palloc_flags flags)
{
  void *k_page_addr = palloc_get_page (PAL_USER | flags);

  /*if there is no free page we evict/swap out a frame to create space
  then recall palloc*/
  if (k_page_addr == NULL)
    {
      struct frame *victim = get_victim_frame ();

      enum vm_page_type type = victim->spte->type;
      int swap_slot = -1;
      uint32_t *victim_pd = victim->owning_thread->pagedir;
      const void *victim_vpage = victim->v_page_addr;
      bool is_dirty = pagedir_is_dirty (victim_pd, victim_vpage);

      /*if it is VM_BIN we only need to write to the swap partition if the page
      is dirty and then change the type to VM_ANON so that if we want this page
      again when page fault happens we get the dirty/changed page from the swap
      parition rather than the old one.*/
      if (type == VM_BIN && is_dirty)
        {
          swap_slot = swap_out (victim->k_page_addr);
          victim->spte->type = VM_ANON;
        }

      /*if it is a VM_FILE then no need to swap_out we can just write changes
       * to the file*/
      if (type == VM_FILE)
        {

          if (is_dirty)
            {
              file_write_at (victim->spte->file, victim->k_page_addr,
                             victim->spte->read_bytes, victim->spte->offset);
              ;
            }
        }

      /*if it is VM_ANON type then just write to swap partition because if we
      don't we will lose the info as VM_ANON pages arent backed by files or
      executables like VM_BIN or VM_FILE*/
      if (type == VM_ANON)
        {
          swap_slot = swap_out (victim->k_page_addr);
        }

      /*get rid of virtual memory to physical memory mapping.
      So page will now pagefault on access*/
      pagedir_clear_page (victim_pd, victim->spte->vaddr);

      /*update info after eviction*/
      victim->spte->in_memory = false;
      if (swap_slot != -1)
        {
          victim->spte->swap_slot = swap_slot;
        }

      /*free the frame's physical page and its tracking struct directly
    (the frame was already removed from the frame table by get_victim_frame) */
      palloc_free_page (victim->k_page_addr);
      free (victim);

      /*re get a page after we evicted one*/
      k_page_addr = palloc_get_page (PAL_USER | flags);
    }

  struct frame *f = malloc (sizeof (struct frame));
  if (f == NULL)
    {
      palloc_free_page (k_page_addr);

      return NULL;
    }

  f->k_page_addr = k_page_addr;
  f->v_page_addr = spte->vaddr;
  f->spte = spte;
  f->owning_thread = thread_current ();
  f->pinned = false;
  f->spte->frame = f;

  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &f->frame_elem);
  lock_release (&frame_table_lock);

  return k_page_addr;
}

/*Frees the frame that lives at the physical memory mapped to the kernal
 * address*/
void
frame_free (void *k_page_addr)
{
  lock_acquire (&frame_table_lock);

  struct list_elem *cur;

  for (cur = list_begin (&frame_table); cur != list_end (&frame_table);
       cur = list_next (cur))
    {
      struct frame *f = list_entry (cur, struct frame, frame_elem);
      if (f->k_page_addr == k_page_addr)
        {

          if (f->spte != NULL)
            {
              f->spte->frame = NULL;
              f->spte->in_memory = false;

              pagedir_clear_page (f->owning_thread->pagedir, f->spte->vaddr);
            }

          list_remove (cur);
          free (f);
          break;
        }
    }
  lock_release (&frame_table_lock);
  palloc_free_page (k_page_addr);
}

void
free_all_proccess_frames ()
{
  lock_acquire (&frame_table_lock);

  struct list_elem *cur = list_begin (&frame_table);

  while (cur != list_end (&frame_table))
    {
      struct frame *f = list_entry (cur, struct frame, frame_elem);

      struct list_elem *next_frame = list_next (cur);
      if (f->owning_thread == thread_current ())
        {

          f->spte->frame = NULL;
          f->spte->in_memory = false;
          if (f->spte != NULL)
            {
              pagedir_clear_page (f->owning_thread->pagedir, f->spte->vaddr);
            }

          void *k_page_addr = f->k_page_addr;

          list_remove (cur);
          free (f);

          palloc_free_page (k_page_addr);
        }

      cur = next_frame;
    }
  lock_release (&frame_table_lock);
}
