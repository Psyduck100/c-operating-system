#include <stdbool.h>
#include <hash.h>
#include "filesys/off_t.h"


/* States the type of vm page*/
enum vm_page_type
{
  VM_BIN, /* A binary file. (ELF executable file)*/
  VM_FILE,   /* Normal file. (for memory mapped files)*/
  VM_ANON, /* Anonymous page (for swapping). */
};

struct supp_page_table_entry {
    bool writable; /* True if writable */
    
    bool in_memory; /*True if there is a physical mapping (in a physical
                      frame). False otherwise*/
     
    void *vaddr; /* Virtual page address*/

    uint32_t read_bytes; /* Number of bytes to read from file */
    uint32_t zero_bytes; /* Remaining bytes at the end of a page to be filled with zeros */
    off_t offset; /* Offset in the file to read from */
    size_t swap_slot; /* Swap slot number */ 
    
    struct file *file; /* File to read from */

    enum vm_page_type type; /* Page Type. */

    struct hash_elem elem; /*hash element*/
    
};

unsigned vm_hash_spte(const struct hash_elem *e, void *aux);
bool vm_hash_spte_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);
void vm_hash_spte_destroy_func (struct hash_elem *e, void *aux);
struct supp_page_table_entry* find_spte (void *vaddr);
bool load_file (struct supp_page_table_entry *spte, void *kaddr);
