#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include <string.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"

extern struct list frame_table;
extern struct lock frame_table_lock;

/*Retuns a hash of a supp_page_table_entry for the supplmental page table*/
unsigned
vm_hash_spte (const struct hash_elem *e, void *aux)
{
  struct supp_page_table_entry *spte;
  spte = hash_entry (e, struct supp_page_table_entry, elem);
  return hash_bytes (&spte->vaddr, sizeof (spte->vaddr));
}

/* Compares the value of two hash elements A and B which are 
   supp_page_table_entrys, given auxiliary data AUX. Returns true 
   if A's vaddr is less than B's vaddr, or false if A's vaddr
   is greater than or equal to B's vaddr. */
bool
vm_hash_spte_less_func (const struct hash_elem *a, const struct hash_elem *b,
                void *aux)
{
    struct supp_page_table_entry *spteA;
    spteA = hash_entry (a, struct supp_page_table_entry, elem);

    struct supp_page_table_entry *spteB;
    spteB = hash_entry (b, struct supp_page_table_entry, elem);

    return (spteA->vaddr < spteB->vaddr);
}

/* Action function created for hash_destroy that frees
   the supplemental page entry in hash_elem e*/
void 
vm_hash_spte_destroy_func (struct hash_elem *e, void *aux){
    struct supp_page_table_entry *spte;
    spte = hash_entry (e, struct supp_page_table_entry, elem);

    free(spte);
}

/* Searches the hash table for a spte with address VADDR
    and returns a pointer to it */
struct supp_page_table_entry* 
find_spte (void *vaddr) {
    struct supp_page_table_entry spte;
    struct hash_elem *e;

    /* Round down bc addresses are almost never page-aligned
        and all entries in the has table are page-aligned
        so not rounding down would always result in null*/
        
    spte.vaddr = pg_round_down(vaddr);
    e = hash_find(&thread_current()->vm, &spte.elem);
    if (e == NULL) {
        return NULL;
    }
    return hash_entry(e, struct supp_page_table_entry, elem);
}

/* Loads a file from the disk to physical memeory using KADDR mapping
    returns true if success, false otherwise */
bool 
load_file (struct supp_page_table_entry *spte, void *kaddr){
    if (spte->file == NULL) {
        return false;
    }
    /* Check if the number of bytes read = the number of bytes to read 
        if they don't match, return false */
    if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) != (int) spte->read_bytes) {
        return false;
    }
    /* Set the rest of the file = 0 */
    memset(kaddr + spte->read_bytes, 0, spte->zero_bytes);
    return true;
}

void frame_table_init (void) {
    list_init(&frame_table);
    lock_init(&frame_table_lock);
}


void *frame_allocate (void *v_page_addr, enum palloc_flags flags){
    void *k_page_addr = palloc_get_page(PAL_USER | flags);
    if (k_page_addr == NULL) {
        //evict: do this later

        return NULL;
    }

    struct frame *f = malloc(sizeof (struct frame));
    if (f == NULL){
        palloc_free_page(k_page_addr);

        return NULL;
    }

    f->k_page_addr = k_page_addr;
    f->v_page_addr = v_page_addr;
    f->owning_thread = thread_current();
    f->pinned = false;

    lock_acquire (&frame_table_lock);
    list_push_back(&frame_table, &f->frame_elem);
    lock_release(&frame_table_lock);


    return k_page_addr;
}

void frame_free(void *k_page_addr){
    lock_acquire(&frame_table_lock);

    struct list_elem *cur;

    for(cur = list_begin(&frame_table); cur != list_end(&frame_table); cur = list_next(cur)){
        struct frame *f = list_entry(cur, struct frame, frame_elem);
        if (f->k_page_addr == k_page_addr){
            list_remove(cur);
            free(f);
            break;
        }


    }
    lock_release(&frame_table_lock);
    palloc_free_page(k_page_addr);

}