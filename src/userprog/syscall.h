#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int mapid_t;

struct lock f_lock;
/*mapid_t mapid;*/

void syscall_init (void);
void sys_halt(void);
void sys_exit(int status);
tid_t sys_exec(const char *cmd_line);
int sys_wait(tid_t pid);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
int fibonacci(int n);
int max_val(int a, int b);
int max_of_four_int(int a, int b, int c, int d);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *flie);
int sys_open(const char *file);
void sys_close(int fd);
int sys_filesize(int fd);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void chk_addr_area(const void *addr, int offset, int end, int bytes);
/**pj4*******************************************************/
void chk_buffer_area(const void *buffer, unsigned size);
/*int sys_mmap(int fd, void *addr);*/
/************************************************************/
#endif /* userprog/syscall.h */
