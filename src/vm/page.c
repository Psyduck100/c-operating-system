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

struct supp_page_table_entry *create_anon_spte(void *vaddr)
{
  
    struct supp_page_table_entry *spte
        = malloc (sizeof (struct supp_page_table_entry));
    if (spte == NULL) return NULL;

    /* Initialize spte fields for anon file bc stack isn't
        backed by any file */
    spte->type = VM_ANON;
    spte->vaddr = vaddr;
    spte->writable = true;
    spte->in_memory = true;
    spte->file = NULL;
    spte->offset = 0;
    spte->read_bytes = 0;
    spte->zero_bytes = PGSIZE;
    // spte->swap_slot = NULL; // not sure what to make this yet

    hash_insert (&thread_current ()->vm, spte);

    return spte;

}





