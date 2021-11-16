#include "vm/swap.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"

#define PPS (PGSIZE / BLOCK_SECTOR_SIZE)
#define SWAP_CNT (block_size(swap_disk) / PPS)

void swap_init(){
  swap_disk = block_get_role(BLOCK_SWAP);
  swap_check = malloc(SWAP_CNT);
  memset(swap_check, 0, sizeof(swap_check));
  lock_init(&s_lock);
}

uint32_t swap_out(void *pfn){
  uint32_t idx = 0;
  lock_acquire(&s_lock);
  while(swap_check[idx] && idx < SWAP_CNT)
	idx++;
  swap_check[idx] = 1;
  for(int i = 0; i < PPS; i++)
	block_write(swap_disk, idx * PPS + i, pfn + i * BLOCK_SECTOR_SIZE);
  lock_release(&s_lock);
  return idx;
}

void swap_in(void *vpn){
  void *kpage = 0;
  struct spt_entry *spte;

  while(!(kpage = palloc_get_page(PAL_USER))){
	page_evict();
  }
  spte = find_spt_entry(vpn);
  lock_acquire(&s_lock);
  for(int i = 0; i < PPS; i++)
	block_read(swap_disk, spte->swap_idx * PPS + i, kpage + i * BLOCK_SECTOR_SIZE);
  pagedir_set_page(spte->t->pagedir, vpn, kpage, spte->writable);
  swap_check[spte->swap_idx] = 0;
  spte->swap_idx = -1;
  spte->pfn = pg_round_down(kpage);
  lock_release(&s_lock);
}
