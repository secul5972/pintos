#ifndef VM_SWAP_H
# define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include "threads/synch.h"

struct block *swap_disk;
struct lock s_lock;
struct bitmap *swap_check;

void swap_init();
uint32_t swap_out(void *pfn);
void swap_in(void *vpn, void *kpage);

#endif;
