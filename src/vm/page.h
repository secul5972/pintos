#ifndef PAGE_H
# define PAGE_H

#include <hash.h>
#include "threads/synch.h"
#include "lib/stdbool.h"

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct spt_entry{
  uint8_t type;
  void *va;
  bool writable;
  
  bool is_loaded;
  struct file* file;

  size_t offset;
  size_t read_bytes;
  size_t zero_bytes;

  struct hash_elem h_elem;
}

void spt_init();
void spt_destroy(struct hash *spt);

