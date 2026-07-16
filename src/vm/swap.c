#include "frame.h"
#include "pagedir.h"
#include "thread.h"
static struct lcok swap_lock;


/*This function searches through the global frame table to find
  a victim frame to evict. Returns the frame to evict. Used when
  there is no more space for a new page.*/
struct frame *get_victim_frame(){

    /*global list so need to acquire lock*/
    lock_acquire(&frame_table_lock);

    struct list_elem *cur_frame_elem = list_begin (&frame_table);

    /*we are implementing the FIFO like second chance */
    while (cur_frame_elem != list_end (&frame_table)) 
    {
        struct frame *cur_frame = list_entry (cur_frame_elem, struct frame, frame_elem);

        uint32_t *cur_pd = cur_frame->owning_thread->pagedir;
        const void *cur_vpage = cur_frame->v_page_addr;

         /*if accessed bit is 1 set it to 0 and move it to the tail*/
         if (pagedir_is_accessed (cur_pd, cur_vpage)) {
            pagedir_set_accessed (cur_pd, cur_vpage, false); 
            
            struct list_elem *next_elem = list_next (cur_frame_elem);
            
            list_remove(cur_frame_elem);
            list_push_back(&frame_table, cur_frame_elem);

            cur_frame_elem = next_elem;
         }
         /*if accessed bit is 0 then this is the victim apge*/
         else {

            /*make sure the victim page is not pinned. If pinned
             need to choose different page*/
            if (!cur_frame->pinned){

                lock_release(&frame_table_lock);

                return cur_frame;
            }

            cur_frame_elem = list_next (cur_frame_elem);

         }
    }

    lock_release(&frame_table_lock);
    return NULL;
}

/* Initializes the swap table bitmap to 0 and the swap lock */
void
swap_init () {
  swapb_block = block_get_role (BLOCK_SWAP);
  if (swapb_block == NULL)
    exit (1);

  size_t swap_sector = block_size (swap_block);
  size_t swap_pages = swap_sector * BLOCK_SECTOR_SIZE / PGSIZE;
  
  swap_table = bitmap_create (swap_pages);
  if (swap_table == NULL)
    exit (1);

  bitmap_set_all (swap_table, false);
  lock_init (&swap_lock); 
}

/* Frees a swap slot */
void
swap_free (size_t swap_slot) {
  lock_acquire (&swap_lock);
  if (!bitmap_test (swap_table, swap_slot)) {
    bitmap_set (swap_table, swap_slot, false);
  }
  lock_release (&swap_lock);
}

/* Swaps out a page to the swap device */
int
swap_out (void *kpage) {
  /* Acquire lock to avoid race conditions */
  lock_acquire (&swap_lock);

  /* Find a free slot to swap */ 
  size_t swap_slot = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (swap_slot == BITMAP_ERROR) {
    exit (1);
  }

  /* Write the page to the swap device */
  size_t sector_offset = swap_slot * (PGSIZE / BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++) {
    block_write (swap_block, sector_offset + i, kpage + i * BLOCK_SECTOR_SIZE);
  }

  lock_release (&swap_lock);
  return swap_slot;
}

void swap_in (size_t swap_slot, void *kpage) {
  /* Acquire lock to avoid race conditions */
  lock_acquire (&swap_lock);

  /* Check slot is not empty */
  if (bitmap_test (swap_table, swap_slot)) {
    exit (1);
  }

  size_t sector_offset = swap_slot * (PGSIZE / BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++) {
    block_read (swap_block, sector_offset + i, (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
  }

  /* Mark the slot as free */
  bitmap_set (swap_table, swap_slot, false);

  /* Release lock */
  lock_release (&swap_lock);

}