#ifndef PAGE_H
# define PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include <hash.h>

struct spt_entry{
  uint8_t type;
  void *va;
  bool writable;
  
  bool is_loaded;
  struct file* file;

  size_t ofs;
  size_t read_bytes;
  size_t zero_bytes;

  struct hash_elem h_elem;
};

void spt_init(void);
bool insert_spte(struct hash *spt, struct spt_entry *spte);
bool delete_spte(struct hash *spt, struct spt_entry *spte);
struct spt_entry *find_spt_entry(void *va);
void spte_free(struct hash_elem *he, void *aux);
bool load_file(void *kpage, struct spt_entry *spte);
bool fault_handler(struct spt_entry *spte);
void spt_destroy(struct hash *spt);
#endif
