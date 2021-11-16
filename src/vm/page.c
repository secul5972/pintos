#include <stdlib.h>
#include <stdlib.h>
#include "page.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

static unsigned spt_hash_func(const struct hash_elem *he, void *aux UNUSED){
  return hash_int(hash_entry(he, struct spt_entry, h_elem)->vpn);
}

static bool spt_less_func(const struct hash_elem *he1, const struct hash_elem *he2, void *aux UNUSED){
  return hash_entry(he1, struct spt_entry, h_elem)->vpn < hash_entry(he2, struct spt_entry, h_elem)->vpn;
}

void spt_init(struct hash *spt){
  hash_init(spt, spt_hash_func, spt_less_func, 0);
  swap_page = palloc_get_page(PAL_USER);
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
  spte.vpn = pg_round_down(va);
  if(!(he = hash_find(&thread_current()->spt, &spte.h_elem))){
	return 0;
  }
  return hash_entry(he, struct spt_entry, h_elem);
}

void spte_free(struct hash_elem *he, void *aux UNUSED){
  free(hash_entry(he, struct spt_entry, h_elem));
}

void spt_destroy(struct hash *spt){
  hash_destroy(spt, spte_free);
}

bool page_evict(){
  struct hash_iterator i;
  struct thread *t = thread_current();
  struct spt_entry *spte = 0;
  while(1){
	hash_first(&i, &t->spt);
	while(hash_next(&i)){
	  spte = hash_entry(hash_cur(&i), struct spt_entry, h_elem);
	  if(!spte->pinned && spte->pfn){
		if(thread_ticks % 3 == 0)
		  continue;
		spte->swap_idx = swap_out(spte->pfn);
		pagedir_clear_page(spte->t->pagedir, spte->pfn);
		palloc_free_page(spte->pfn);
		spte->pfn = 0;
		return true;
	  }
	}
  }
}
