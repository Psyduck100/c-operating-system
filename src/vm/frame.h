#include "threads/palloc.h"
#include <list.h>
#include "threads/synch.h"
#include "userprog/pagedir.h"


struct frame {
  void *k_page_addr; //kernal address of the page/frame as pintos maps kernal to phys
  void *v_page_addr; //virtual address
  struct thread * owning_thread; //thread that owns this page
  struct supp_page_table_entry *spte; 
  struct list_elem frame_elem;
  bool pinned;
};

extern struct list frame_table;
extern struct lock frame_table_lock;
void frame_table_init(void);
void *frame_allocate(struct supp_page_table_entry *spte, enum palloc_flags flags);
void frame_free(void *k_page_addr);