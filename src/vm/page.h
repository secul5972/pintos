#ifndef PAGE_H
# define PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include <hash.h>
#include <stdbool.h>

struct spt_entry{
  void *vpn;
  void *pfn;

  bool writable;
  bool pinned;

  int32_t swap_idx;

  struct hash_elem h_elem;
  struct thread *t;
};

void *swap_page;

void spt_init(struct hash *spt);
bool insert_spte(struct hash *spt, struct spt_entry *spte);
bool delete_spte(struct hash *spt, struct spt_entry *spte);
struct spt_entry *find_spt_entry(void *va);
void spte_free(struct hash_elem *he, void *aux);
void spt_destroy(struct hash *spt);
bool page_evict();

#endif
