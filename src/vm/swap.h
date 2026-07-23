#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

struct frame *get_victim_frame (void);
void swap_init (void);
void swap_free (size_t swap_slot);
int swap_out (void *kpage);
void swap_in (size_t swap_slot, void *kpage);

#endif