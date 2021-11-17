#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  
  char parsed_name[30];
  int i = 0;
  while(file_name[i] && file_name[i] != ' '){
	parsed_name[i] = file_name[i];
    i++;
  }
  parsed_name[i] = 0;
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (parsed_name, PRI_DEFAULT, start_process, fn_copy);

  /**pj2**************************************************/
  sema_down(&thread_current()->l_sema);
  /*******************************************************/

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 

 /**pj2**************************************************/
  if(find_thread(-1))
	return process_wait(tid);
 /*******************************************************/

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /**pj4**************************************************/
  spt_init(&thread_current()->spt);
  /*******************************************************/
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  

  success = load (file_name, &if_.eip, &if_.esp);
  
  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) {
	/**pj2*********************************************/
	//if load fail, thread_error
	thread_current()->flag = 1;
	sys_exit(-1);
	/**************************************************/
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

/**pj1*********************************************************/
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *t = find_thread(child_tid);
  int status = -1;
  if(t){
	sema_down(&t->c_sema);
	status = t->exit_status;
	list_remove(&t->c_elem);
  //inserted for syn_read, syn_write
  //previous synchronization system can't pass syn_read, write
	sema_up(&t->m_sema);
  }
  return status;
}
/**************************************************************/

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  spt_destroy(&cur->spt);
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
/**pj1*******************************************************/
  //delete thread in waiting list
  sema_up(&cur->c_sema);
/**pj2*******************************************************/
  //inserted for syn_read, syn_write
  //previous synchronization system can't pass syn_read, write
  sema_down(&cur->m_sema);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  
  /**pj1***********************************************/
  char  *tmp_ptr, **tok;
  int tok_cnt = 1, s_flag = 1;

  i = 0;

  //count word
  while(file_name[i]){
	if(file_name[i] == ' ' && s_flag)
	  tok_cnt++, s_flag = 0;
	else if(file_name[i] != ' ')
	  s_flag = 1;
	i++;
  }

  //malloc and parsing
  tok = malloc(sizeof(char *) * tok_cnt);

  //file_name = echo x -> echo
  tok[0] = strtok_r(file_name, " ", &tmp_ptr);
  i = 1;
  while(i < tok_cnt){
	tok[i++] = strtok_r(NULL, " ", &tmp_ptr);
  }
  if (tok[tok_cnt-1] == 0)
	tok_cnt--;
  /****************************************************/

  /* Open executable file. */
  file = filesys_open (file_name);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  //t->t_file = file;
  //file_deny_write(file);
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /**pj1***********************************************/
  int len, used_byte = 0;
  char **tok_ptr;
  i = tok_cnt - 1;
 
  //save each token address
  tok_ptr = malloc(sizeof(char *) * tok_cnt);

  //push tok and count used_byte
  while(i >= 0){
	len = strlen(tok[i]) + 1;
	*esp -= len;
	strlcpy(*esp, tok[i], len);
	tok_ptr[i--] = *esp;
	used_byte += len;
  }
  free(tok);

  //word alignment
  *esp -= (4 - used_byte % 4) % 4;

  //0
  *esp -= 4;
  //uint32_t == 4byte
  **(uint32_t **)esp = 0;

  //push tok_ptr
  i = tok_cnt - 1;
  while(i >= 0){
	*esp -= 4;
	**(uint32_t **)esp = tok_ptr[i--];
  }
  free(tok_ptr);

  //push previous *esp
  *esp -= 4;
  **(uint32_t**)esp = *esp + 4;

  //push tok_cnt
  *esp -= 4;
  **(uint32_t **)esp = tok_cnt;

  //dummy return address
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /****************************************************/

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //if(!success)
  file_close (file);
  /**pj2************************************************/
  sema_up(&thread_current()->pa->l_sema);
  /*****************************************************/
  return success;
}

/* load() helpers. */

bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
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
	  if(kpage != swap_page){
		if(!install_page(upage, kpage, writable))
		{
		  palloc_free_page(kpage);
		  return false;
		}
	  }
  /**pj4******************************************************/
	  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
	  spte->vpn = pg_round_down(spte->vpn = upage);
	  spte->writable = writable;
	  spte->pinned = 0;
	  spte->pfn = pg_round_down(kpage);
	  spte->t = thread_current();
	  if(kpage == swap_page)
		spte->swap_idx = swap_out(kpage);
	  else spte->swap_idx = -1;
	  if(!insert_spte(&thread_current()->spt, spte)){
		palloc_free_page(kpage);
		free(spte);
		return false;
	  }
  /***********************************************************/

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

    /**pj4******************************************************/
  while(!(kpage = palloc_get_page (PAL_USER | PAL_ZERO)))
	page_evict();

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (success)
	*esp = PHYS_BASE;
  else
	palloc_free_page (kpage);
  
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  spte->vpn = pg_round_down(((uint8_t *) PHYS_BASE) - PGSIZE);
  spte->writable = 1;
  spte->pinned = 1;
  spte->swap_idx = -1;
  spte->t = thread_current();
  spte->pfn = pg_round_down(kpage);
  if(!insert_spte(&thread_current()->spt, spte)){
	palloc_free_page(kpage);
	free(spte);
	success = false;
  }
  /***********************************************************/
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/**pj2******************************************************/
struct thread *find_thread(int child_tid){
  struct thread *t = 0;
  struct list_elem *e_head, *e_end, *e_curr;
  e_head = list_head(&thread_current()->c_list);
  e_end = list_end(e_head);
  for(e_curr = e_head; e_curr != e_end; e_curr = list_next(e_curr)){
	//check all child thread
    t = list_entry(e_curr, struct thread, c_elem);
	//flag == 1 is error thread
	if(child_tid == -1 && t->flag)
	  return t;
	if(child_tid == t->tid)
	  return t;
  }
  return 0;
}

/***********************************************************/
