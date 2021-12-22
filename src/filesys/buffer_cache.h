#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include <stdbool.h>
#include <threads/synch.h>
#include <devices/block.h>
#include <filesys/off_t.h>

#define NUM_CACHE 64

struct buffer_cache_entry{
  bool valid_bit;
  bool reference_bit;
  bool dirty_bit;
  block_sector_t disk_sector;
  uint8_t buffer[BLOCK_SECTOR_SIZE];
};

static struct buffer_cache_entry cache[NUM_CACHE];
struct lock bc_lock;

void buffer_cache_init(void);
bool buffer_cache_read(block_sector_t sector, void *buffer, off_t ofs, int chunk_size, int sector_ofs);
bool buffer_cache_write(block_sector_t sector, void *buffer, off_t ofs, int chunk_size, int sector_ofs);
struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sector);
struct buffer_cache_entry *buffer_cache_select_victim(void);
void buffer_cache_flush_entry(struct buffer_cache_entry *bce);
void buffer_cache_terminate(void);
#endif
