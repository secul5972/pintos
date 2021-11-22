#ifndef VM_SWAP_H
# define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include "threads/synch.h"

struct bitmap *swap_check;

void swap_init();
uint32_t swap_out(void *pfn);
void swap_in(void *vpn, void *kpage);
void clear_block(int idx);

#endif;
