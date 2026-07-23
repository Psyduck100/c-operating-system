#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include <list.h>

/*this struct represents/is an abstraction of a page worth of phyiscal memory*/
struct frame
{
  void *k_page_addr; // kernal address of the page/frame as pintos maps kernal
                     // to phys
  void *v_page_addr; // virtual address
  struct thread *owning_thread;       // thread that owns this page
  struct supp_page_table_entry *spte; /*the spte that links to this frame*/
  struct list_elem frame_elem;        /*the list elem*/
  bool pinned; /*whether or not this frame is pinned so we don't evict it if
                  so*/
};

extern struct list
    frame_table; /*global frame table, list of all frame objects*/
extern struct lock
    frame_table_lock; /*lock for the shared frame table resource*/
void frame_table_init (void);
void *frame_allocate (struct supp_page_table_entry *spte,
                      enum palloc_flags flags);
void frame_free (void *k_page_addr);
void free_all_proccess_frames (void);