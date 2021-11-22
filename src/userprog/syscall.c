#include <stdbool.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "pagedir.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "threads/palloc.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  lock_init(&f_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/**pj1****************************************************/
//check is in user address area
void chk_addr_area(const void *addr, int offset, int end, int bytes){
  for(int i = offset; i <= end; i += bytes){
	//check address area
	if(!(addr + i) || !is_user_vaddr(addr + i)){
	  sys_exit(-1);
	}
	struct spt_entry *spte = find_spt_entry(addr + i);

	if(spte && spte->swap_idx != -1){
	  void *kpage = 0;
	  while(!(kpage = palloc_get_page(PAL_USER)))
		page_evict();
	  swap_in(spte->vpn, kpage);
	  if(!install_page(spte->vpn, kpage, spte->writable)){
		palloc_free_page(kpage);
		sys_exit(-1);
	  }
	  spte->pinned = 1;
	}
  }
}

/**pj4***************************************************/
void chk_buffer_area(const void *buffer, unsigned size, const void *esp){
  if(!(buffer) || !is_user_vaddr(buffer)){
	  sys_exit(-1);
	}
  void *s_vpn = pg_round_down(buffer);
  void *e_vpn = pg_round_down(buffer + size + PGSIZE);
  for(; s_vpn != e_vpn; s_vpn += PGSIZE){
	struct spt_entry *spte = find_spt_entry(s_vpn);
	if(spte && spte->swap_idx != -1){
	  void *kpage = 0;
	  while(!(kpage = palloc_get_page(PAL_USER)))
		page_evict();
	  swap_in(spte->vpn, kpage);
	  if(!install_page(spte->vpn, kpage, spte->writable)){
		palloc_free_page(kpage);
		sys_exit(-1);
	  }
	  spte->pinned = 1;
	}
  }
}
/********************************************************/

void sys_halt(void){
  shutdown_power_off();
}

void sys_exit(int status){
  struct thread *t = thread_current();
  //termination messages
  printf("%s: exit(%d)\n", thread_name(), status);
  //store status in pointer value
  t->exit_status = status;
  //close file descriptor
  int i = t->fd_cnt - 1;
  while(i >= 2)
	sys_close(i--);
  thread_exit();
}

tid_t sys_exec(const char *cmd_line){
  char exec_name[30];
  int i = 0;
 
  chk_addr_area(cmd_line, 0, 0, 4);
  //parsing for print
  while (cmd_line[i] && cmd_line[i] != ' '){
	exec_name[i] = cmd_line[i];
	i++;
  }
  exec_name[i] = 0;

  //check wrong exec_name
  lock_acquire(&f_lock);
  struct file *f = filesys_open(exec_name);
  lock_release(&f_lock);
  if(!f)
	return -1;
  file_close(f);
  return process_execute(cmd_line);
}

int sys_wait(tid_t pid){
  return process_wait(pid);
}

int sys_read(int fd, void *buffer, unsigned size, void *esp){
  chk_buffer_area(buffer, size, esp);
  struct thread *t = thread_current();
  int i = 0;
  if(fd == 0){
	//save one char by one
	lock_acquire(&f_lock);
	while(i < size)
	  ((char *)buffer)[i++] = input_getc();
	//if don't loop 'size' time , error
	lock_release(&f_lock);
	return i;
  }
  else if(2 <= fd && fd < t->fd_cnt){
	//read using file descriptor
	lock_acquire(&f_lock);
	int ret = file_read(t->fd[fd], buffer, size);
	lock_release(&f_lock);
	return ret;
  }
  return -1;
}

int sys_write(int fd, const void *buffer, unsigned size, void *esp){
  chk_buffer_area(buffer, size, esp);
  struct thread *t = thread_current();
  if(fd == 1){
	lock_acquire(&f_lock);
	putbuf(buffer, size);
	lock_release(&f_lock);
	return size;
  }
  else if(2 <= fd && fd < t->fd_cnt){
	//write using file descriptor
	lock_acquire(&f_lock);
	//deny writing executing file
	chk_deny_write(t->fd[fd], 0, 0);
	int ret = file_write(t->fd[fd], buffer, size);
	lock_release(&f_lock);
	return ret;
  }
  return -1;
}

int fibonacci(int n){
  int p = 0, q = 1, tmp;
  for(int i = 0;i < n; i++){
	tmp = q;
	q += p;
	p = tmp;
  }
  return p;
}

int max_val(int a, int b){
  if(a > b) return a;
  else return b;
}

int max_of_four_int(int a, int b, int c, int d){
  return max_val(max_val(a, b), max_val(c, d));
}
/**pj2****************************************************/
bool sys_create(const char *file, unsigned initial_size){
  chk_addr_area(file, 0, 0, 4);
  lock_acquire(&f_lock);
  int ret = filesys_create(file, initial_size);
  lock_release(&f_lock);
  return ret;
}

bool sys_remove(const char *file){
  chk_addr_area(file, 0, 0, 4);
  lock_acquire(&f_lock);
  int ret = filesys_remove(file);
  lock_release(&f_lock);
  return ret;
}

int sys_open(const char *file){
  chk_addr_area(file, 0, 0, 4);
  struct thread *t = thread_current();
  lock_acquire(&f_lock);
  struct file *f = filesys_open(file);
  lock_release(&f_lock);
  if(f){
	if(t->fd_cnt < 128){
	  //file == thread_name -> deny_write
	  chk_deny_write(f, t->name, file);
	  t->fd[t->fd_cnt++] = f;
	  return t->fd_cnt - 1;
	}
  }
  return -1;
}

void sys_close(int fd){
 struct thread *t = thread_current();
 if(fd < 2 || fd >= t->fd_cnt)
   sys_exit(-1);
 lock_acquire(&f_lock);
 file_close(t->fd[fd]);
 lock_release(&f_lock);
 t->fd[fd] = 0;
}

int sys_filesize(int fd){
  lock_acquire(&f_lock);
  int ret =  file_length(thread_current()->fd[fd]);
  lock_release(&f_lock);
  return ret;
}

void sys_seek(int fd, unsigned position){
  lock_acquire(&f_lock);
  file_seek(thread_current()->fd[fd], position);
  lock_release(&f_lock);
}

unsigned sys_tell(int fd){
  lock_acquire(&f_lock);
  int ret =  file_tell(thread_current()->fd[fd]);
  lock_release(&f_lock);
  return ret;
}
/**pj4****************************************************/
int sys_mmap(int fd, void *addr){
  chk_addr_area(addr, 0, 0, 4);
  struct file *file = file_reopen(thread_current()->fd[fd]);
  uint32_t read_bytes = file_length(file);
  uint32_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
  void *upage = addr;
  int mapid = list_size(&thread_current()->m_list);
  int32_t ofs = 0;
  
  //file_seek(file, 0);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
	  
	  uint8_t *kpage;
	  while(!(kpage = palloc_get_page (PAL_USER | PAL_ZERO)))
		page_evict();

	  if(file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
		palloc_free_page(kpage);
		return false;
		}
	  memset(kpage + page_read_bytes, 0, page_zero_bytes);
	  if(!install_page(upage, kpage, 1))
		{
		  palloc_free_page(kpage);
		  return false;
		}
	  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
	  spte->vpn = pg_round_down(upage);
	  spte->writable = 1;
	  spte->pinned = 0;
	  spte->pfn = pg_round_down(kpage);
	  spte->t = thread_current();
	  spte->swap_idx = -1;
	  spte->mapid = mapid;
	  spte->file = file;
	  spte->ofs = ofs;
	  spte->read_bytes = page_read_bytes;

	  list_push_back(&thread_current()->m_list, &spte->m_elem);
	  if(!insert_spte(&thread_current()->spt, spte)){
		palloc_free_page(kpage);
		free(spte);
		return false;
	  }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
	  ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return mapid;

}

void sys_munmap(mapid_t mapping){
  struct thread *t = thread_current();
  struct spt_entry *spte = 0;
  struct list_elem *e_curr, *e_end;

  e_curr = list_begin(&t->m_list);
  e_end = list_end(&t->m_list);
  while(e_curr != e_end){
    spte = list_entry(e_curr, struct spt_entry, m_elem);
	if(!spte->mapid){
	  if(spte->swap_idx != -1){
	    void *kpage = 0;
		while(!(kpage = palloc_get_page(PAL_USER)))
		  page_evict();
		
		swap_in(spte->vpn, kpage);
		if(!install_page(spte->vpn, kpage, spte->writable)){
		  palloc_free_page(kpage);
		  sys_exit(-1);
		}
		lock_acquire(&f_lock);
		file_write_at(spte->file, spte->vpn, spte->read_bytes, spte->ofs);
		lock_release(&f_lock);
		delete_spte(&t->spt, spte);
		pagedir_clear_page(spte->t->pagedir, spte->vpn);
		palloc_free_page(spte->pfn);
		free(spte);
	  }
	  e_curr = list_remove(e_curr);
    }
	else
	  e_curr = list_next(e_curr);
  }
}
/*********************************************************/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_num;
  chk_addr_area(f->esp, 0, 0, 4);
  sys_num = *(uint32_t *)f->esp;
 
  //first check addr, second call system_call function
  switch(sys_num){
	case SYS_HALT:
	  sys_halt();
	  break;
	case SYS_EXIT:
	  chk_addr_area(f->esp, 4, 4, 4);
	  sys_exit(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_EXEC:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_exec((char *)*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_WAIT:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_wait(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_READ:
	  chk_addr_area(f->esp, 4, 12, 4);
	  f->eax = sys_read(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8), *(uint32_t *)(f->esp + 12), *(uint32_t *)f->esp);
	  break;
	case SYS_WRITE:
	  chk_addr_area(f->esp, 4, 12, 4);
	  f->eax = sys_write(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8), *(uint32_t *)(f->esp + 12), *(uint32_t *)f->esp);
	  break;
	case SYS_FIBO:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = fibonacci(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_MAX_FOUR:
	  chk_addr_area(f->esp, 4, 16, 4);
	  f->eax = max_of_four_int(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8), *(uint32_t *)(f->esp + 12), *(uint32_t *)(f->esp + 16));
	  break;
	case SYS_CREATE:
	  chk_addr_area(f->esp, 4, 8, 4);
	  f->eax = sys_create((char *)*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8));
	  break;
	case SYS_REMOVE:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_remove((char *)*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_OPEN:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_open((char *)*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_CLOSE:
	  chk_addr_area(f->esp, 4, 4, 4);
	  sys_close(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_FILESIZE:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_filesize(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_SEEK:
	  chk_addr_area(f->esp, 4, 8, 4);
	  sys_seek(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8));
	  break;
	case SYS_TELL:
	  chk_addr_area(f->esp, 4, 4, 4);
	  f->eax = sys_tell(*(uint32_t *)(f->esp + 4));
	  break;
	case SYS_MMAP:
	  chk_addr_area(f->esp, 4, 8, 4);
	  f->eax = sys_mmap(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8));
	  break;
	case SYS_MUNMAP:
	  chk_addr_area(f->esp, 4, 4, 4);
	  sys_munmap(*(uint32_t *)(f->esp + 4));
  }
}

/*********************************************************/
