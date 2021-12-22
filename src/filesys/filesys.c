#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "filesys/buffer_cache.h"

struct dir* path_parsing(const char *path, char *file_name);

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  buffer_cache_init();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  thread_current()->t_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  //buffer_cache_terminate();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  char file_name[PATH_LEN + 1];
  struct dir *dir = path_parsing(path, file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  char file_name[PATH_LEN + 1];
  struct dir *dir = path_parsing(path, file_name);
  struct inode *inode = NULL;
  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  else return 0;
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  char file_name[PATH_LEN + 1];
  char tmp[PATH_LEN + 1];
  struct dir *dir = path_parsing(path, file_name);

  struct inode *inode;
  dir_lookup(dir, file_name, &inode);
  struct dir * cur_dir = 0;

  bool success = 0;
  if(!inode_isdir(inode) || ((cur_dir = dir_open(inode)) && !dir_readdir(cur_dir, tmp)))
	success = dir != NULL && dir_remove(dir, file_name);
  dir_close (dir); 
  if(cur_dir)
	dir_close(cur_dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  struct dir *root = dir_open_root();
  dir_add(root, ".", ROOT_DIR_SECTOR);
  dir_add(root, "..", ROOT_DIR_SECTOR);
  dir_close(root);

  free_map_close ();
  printf ("done.\n");
}

struct dir* path_parsing(const char *path, char *file_name){
  struct dir *dir;
  char tmp[PATH_LEN + 1];
  char *token, *token2, *save_ptr; 
  if(!path || !file_name)
	return 0;
  if(strlen(path) == 0)
	return 0;
  strlcpy(tmp, path, PATH_LEN);

  if(tmp[0] == '/')	dir = dir_open_root();
  else dir = dir_reopen(thread_current()->t_dir);

  if(!inode_isdir(dir_get_inode(dir)))
    return 0;
  
  token = strtok_r(tmp, "/", &save_ptr);
  if(!token){
    strlcpy(file_name, ".", PATH_LEN);
	return dir;
  }
  token2 = strtok_r(0, "/", &save_ptr);
  while(token && token2){
	struct inode *inode = 0;
	if(!dir_lookup(dir, token, &inode)){
	  dir_close(dir);
	  return 0;
	}
	if(!inode_isdir(inode)){
	  dir_close(dir);
	  return 0;
	}
	dir_close(dir);
	dir = dir_open(inode);
	token = token2;
	token2 = strtok_r(0, "/", &save_ptr);
  }
  strlcpy(file_name, token, PATH_LEN);
  return dir;
}

bool filesys_create_dir(const char *path){
  block_sector_t inode_sector = 0;
  char file_name[PATH_LEN + 1];
  struct dir *dir = path_parsing(path, file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create(inode_sector, 16)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  if(success){
	struct dir *new_dir = dir_open(inode_open(inode_sector));
	dir_add(new_dir, ".", inode_sector);
	dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
	dir_close(new_dir);
  }
  dir_close (dir);
  return success;
}
