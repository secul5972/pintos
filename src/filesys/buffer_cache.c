#include <string.h>
#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"

static int clock_hand;

void buffer_cache_init(void){
  for(int i = 0; i < NUM_CACHE; i++){
	cache[i].valid_bit = 0;
	cache[i].reference_bit = 0;
	cache[i].dirty_bit = 0;
	memset(cache[i].buffer, 0, sizeof(struct buffer_cache_entry));
  }
  lock_init(&bc_lock);
  clock_hand = 0;
}

bool buffer_cache_read(block_sector_t sector, void *buffer, off_t ofs, int chunk_size, int sector_ofs){
  struct buffer_cache_entry *bce = buffer_cache_lookup(sector);
  if(!bce){
	bce = buffer_cache_select_victim();
	buffer_cache_flush_entry(bce);

	lock_acquire(&bc_lock);
	bce->valid_bit = 1;
	bce->disk_sector = sector;
	block_read(fs_device, sector, bce->buffer);
	lock_release(&bc_lock);
	
  }
  bce->reference_bit = 1;
  lock_acquire(&bc_lock);
  memcpy (buffer + ofs, bce->buffer + sector_ofs, chunk_size);
  lock_release(&bc_lock);
  return true;
}

bool buffer_cache_write(block_sector_t sector, void *buffer, off_t ofs, int chunk_size, int sector_ofs){
  struct buffer_cache_entry *bce = buffer_cache_lookup(sector);
  if(!bce){
	bce = buffer_cache_select_victim();
	buffer_cache_flush_entry(bce);

	lock_acquire(&bc_lock);
	bce->valid_bit = 1;
	bce->disk_sector = sector;
	block_read(fs_device, sector, bce->buffer);
	lock_release(&bc_lock);
	
  }
  lock_acquire(&bc_lock);
  bce->dirty_bit = 1;
  bce->reference_bit = 1;
  memcpy (bce->buffer + sector_ofs, buffer + ofs, chunk_size);
  lock_release(&bc_lock);
  return true;
}

struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sector){
  struct buffer_cache_entry *bce;
  lock_acquire(&bc_lock);
  for(int i = 0; i < NUM_CACHE; i++){
	bce = &cache[i];
	if(bce->valid_bit && bce->disk_sector == sector){
	  lock_release(&bc_lock);
	  return bce;
	}
  }
  lock_release(&bc_lock);
  return 0;
}

struct buffer_cache_entry *buffer_cache_select_victim(void){
  struct buffer_cache_entry *bce;
  lock_acquire(&bc_lock);
  while(1){
	bce = &cache[clock_hand];
	if(bce->reference_bit) bce->reference_bit = 0;
	else{
	  lock_release(&bc_lock);
	  return bce;
	}
	clock_hand = (clock_hand + 1) % NUM_CACHE;
  }
  NOT_REACHTED();
}

void buffer_cache_flush_entry(struct buffer_cache_entry *bce){
  if(bce->valid_bit && bce->dirty_bit){
	lock_acquire(&bc_lock);
	block_write(fs_device, bce->disk_sector, bce->buffer);
	bce->dirty_bit = 0;
	lock_release(&bc_lock);
  }
}

void buffer_cache_terminate(void){
  for(int i = 0; i < NUM_CACHE; i++){
	buffer_cache_flush_entry(&cache[i]);
  }
}
