#include "page.h"

static unsigned spt_hash_func(const struct hast_elem *he, void *aus){
  return hast_int(hash_entry(he, struct spt_entry, h_elem)->va);
}

static bool vm_less_func(const struct hash_elem *he1, const struct hash_elem *he2, void *aus){
  return hash_entry(he1, struct spt_entry, h_elem)->va < hash_entry(he2, struct spt_entry, h_elem)->va;
}

void spt_init(){

  hash_init(&current_thread()->spt, spt_hash_func, spt_less_func, 0);
}

bool insert_spte(struct hash *spt, struct spt_entry *spt_e){
  return !hash_insert(spt, spt_e);
}

bool delete_spte(struct hash *spt, struct spt_entry *spt_e){
  return hash_delete(spt, spt_e);
}

struct spt_entry *find_spt_entry(void *va){
  struct spt_entry spte;
  struct hash_elem he;

  //get page number
  spt_e->va = pg_round_down(va);
  if(!(he = hash_find(&current_thread()->spt, &spt.elem))
	return 0;
  return hash_entry(he, struct spt_entry, h_elem);
}

void spte_free(struct hash_elem *he, void *aux){
  free(hash_entry(he, struct spt_entry, h_elem));
}

void spt_destroy(struct hash *spt){
  hash_destroy(spt, spte_free);
}
