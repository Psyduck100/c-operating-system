#ifndef VM_PAGE_H
#define VM_PAGE_H
#include "filesys/off_t.h"
#include "threads/palloc.h"
#include <hash.h>
#include <stdbool.h>

/* Mapid_t def */
typedef int mapid_t;

/* States the type of vm page*/
enum vm_page_type
{
  VM_BIN,  /* A binary file. (ELF executable file)*/
  VM_FILE, /* Normal file. (for memory mapped files)*/
  VM_ANON, /* Anonymous page (for swapping). */
};

struct supp_page_table_entry
{
  bool writable; /* True if writable */

  bool in_memory; /*True if there is a physical mapping (in a physical
                    frame). False otherwise*/

  void *vaddr; /* Virtual page address*/

  uint32_t read_bytes; /* Number of bytes to read from file */
  uint32_t zero_bytes; /* Remaining bytes at the end of a page to be filled
                          with zeros */
  off_t offset;        /* Offset in the file to read from */

  struct file *file; /* File to read from */

  enum vm_page_type type; /* Page Type. */

  struct hash_elem elem; /*hash element*/

  struct list_elem mmap_elem; /*mmap elem for the mmap list of sptes*/

  struct frame *frame; /* frame for the virtual page (need to allocate when
                          frame.h is readded)*/
  int swap_slot; /*the place in the swap disk that the page was swapped to*/
};

struct mmap_file
{
  mapid_t mapid;         /* The mapping ID. */
  struct file *file;     /* The file being mapped. */
  struct list spte_list; /* List of supplemental page table entries for this
                            mapping. */
  struct list_elem
      elem; /* List element for the list of memory-mapped files. */
};

unsigned vm_hash_spte (const struct hash_elem *e, void *aux);
bool vm_hash_spte_less_func (const struct hash_elem *a,
                             const struct hash_elem *b, void *aux);
void vm_hash_spte_destroy_func (struct hash_elem *e, void *aux);
struct supp_page_table_entry *find_spte (const void *vaddr);
bool load_file (struct supp_page_table_entry *spte, void *kaddr);
struct supp_page_table_entry *expand_stack (void *fault_addr);
bool vm_page_fault_helper (struct supp_page_table_entry *spte);

struct supp_page_table_entry *create_anon_spte (void *vaddr);
#endif
