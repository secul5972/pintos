#include "vm/swap.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"

static struct block *swap_disk;
static struct lock s_lock;

void swap_init(){
  swap_disk = block_get_role(BLOCK_SWAP);
  swap_check = bitmap_create(block_size(swap_disk) / 8);
  bitmap_set_all(swap_check, 0);
  lock_init(&s_lock);
}

uint32_t swap_out(void *pfn){
  lock_acquire(&s_lock);
  uint32_t idx = bitmap_scan(swap_check, 0, 1, 0);
  for(int i = 0; i < 8; i++){
	block_write(swap_disk, idx * 8 + i, pfn + i * BLOCK_SECTOR_SIZE);
  }
  bitmap_flip(swap_check, idx);
  lock_release(&s_lock);
  return idx;
}

void swap_in(void *vpn, void *kpage){
  struct spt_entry *spte = find_spt_entry(vpn);
  lock_acquire(&s_lock);
  for(int i = 0; i < 8; i++)
	block_read(swap_disk, spte->swap_idx * 8 + i, kpage + i * BLOCK_SECTOR_SIZE);
  bitmap_flip(swap_check, spte->swap_idx);
  spte->swap_idx = -1;
  spte->pfn = pg_round_down(kpage);
  spte->t = thread_current();
  lock_release(&s_lock);
}
