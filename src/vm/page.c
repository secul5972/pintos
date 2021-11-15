#include <stdlib.h>
#include <stdlib.h>
#include <stdbool.h>
#include "page.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static unsigned spt_hash_func(const struct hash_elem *he, void *aux UNUSED){
  return hash_int(hash_entry(he, struct spt_entry, h_elem)->va);
}

static bool spt_less_func(const struct hash_elem *he1, const struct hash_elem *he2, void *aux UNUSED){
  return hash_entry(he1, struct spt_entry, h_elem)->va < hash_entry(he2, struct spt_entry, h_elem)->va;
}

void spt_init(void){
  hash_init(&thread_current()->spt, spt_hash_func, spt_less_func, 0);
}

bool insert_spte(struct hash *spt, struct spt_entry *spte){
  return !hash_insert(spt, &spte->h_elem);
}

bool delete_spte(struct hash *spt, struct spt_entry *spte){
  return hash_delete(spt, &spte->h_elem);
}

struct spt_entry *find_spt_entry(void *va){
  struct spt_entry spte;
  struct hash_elem *he;

  //get page number
  spte.va = pg_round_down(va);
  if(!(he = hash_find(&thread_current()->spt, &spte.h_elem)))
	return 0;
  return hash_entry(he, struct spt_entry, h_elem);
}

void spte_free(struct hash_elem *he, void *aux UNUSED){
  free(hash_entry(he, struct spt_entry, h_elem));
}

void spt_destroy(struct hash *spt){
  hash_destroy(spt, spte_free);
}

bool fault_handler(struct spt_entry *spte){
  uint8_t *kpage = palloc_get_page(PAL_USER);
  printf("%p\n", spte);
  if(!load_file(kpage, spte) || !install_page(spte->va, kpage, spte->writable)){
	palloc_free_page(kpage);
	return false;
  }
}


