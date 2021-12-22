#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer_cache.h"

#define DIRECT_ENTRIES 123
#define INDIRECT_ENTRIES 128

static char zeros[BLOCK_SECTOR_SIZE];
block_sector_t f_block[INDIRECT_ENTRIES];
block_sector_t s_block[INDIRECT_ENTRIES];

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
	uint32_t is_dir; 
    off_t length;                     /* File size in bytes. */
    unsigned magic;                   /* Magic number. */
	uint32_t direct[DIRECT_ENTRIES];
	uint32_t indirect;
	uint32_t d_indirect;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct lock i_lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */

void free_inode(struct inode_disk *disk_inode);
bool grow_inode_disk(struct inode_disk *disk_inode, off_t length);

static block_sector_t
byte_to_sector (const struct inode_disk *disk_inode, off_t pos) 
{
  ASSERT (disk_inode != NULL);
  size_t sector = pos / BLOCK_SECTOR_SIZE;
  if(sector < DIRECT_ENTRIES)
	return disk_inode->direct[sector];
  else if(sector < DIRECT_ENTRIES + INDIRECT_ENTRIES){
	buffer_cache_read(disk_inode->indirect, f_block, 0, BLOCK_SECTOR_SIZE, 0);
	return f_block[sector - DIRECT_ENTRIES];
  }
  else if(sector < DIRECT_ENTRIES + INDIRECT_ENTRIES * (INDIRECT_ENTRIES + 1)){
	block_sector_t f_idx = (sector - DIRECT_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
    block_sector_t s_idx = (sector - DIRECT_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
	buffer_cache_read(disk_inode->d_indirect, f_block, 0, BLOCK_SECTOR_SIZE, 0);
	buffer_cache_read(f_block[f_idx], s_block, 0, BLOCK_SECTOR_SIZE, 0);
	return s_block[s_idx];
  }
  else
	return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
//  memset(zeros, 0, BLOCK_SECTOR_SIZE);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  bool success = false;
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors(length);
	  int i;

      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
	  disk_inode->is_dir = is_dir;

	  for(i = 0; i < DIRECT_ENTRIES && sectors - i > 0; i++){
		if(!free_map_allocate(1, &disk_inode->direct[i]))
		  return false;
		block_write(fs_device, disk_inode->direct[i], zeros);
	  }

	  if(sectors >= DIRECT_ENTRIES){
		if(!free_map_allocate(1, &disk_inode->indirect))
		  return false;
		memset(f_block, 0, BLOCK_SECTOR_SIZE);
		for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES && sectors - i > 0; i++){
		  if(!free_map_allocate(1, &f_block[i - DIRECT_ENTRIES]))
			return false;
		  block_write(fs_device, f_block[i - DIRECT_ENTRIES], zeros);
		}
		block_write(fs_device, disk_inode->indirect, f_block);
	  }

	  if(sectors >= DIRECT_ENTRIES + INDIRECT_ENTRIES){
		if(!free_map_allocate(1, &disk_inode->d_indirect))
		  return false;
		memset(f_block, 0, BLOCK_SECTOR_SIZE);
		for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES * (INDIRECT_ENTRIES + 1) && sectors - i > 0; i++){
		  block_sector_t f_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
		  block_sector_t s_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
		  if(s_idx == 0){
			if(!free_map_allocate(1, &f_block[f_idx]))
			  return false;
			memset(s_block, 0, BLOCK_SECTOR_SIZE);
		  }
		  if(!free_map_allocate(1, &s_block[s_idx]))
			return false;
		  block_write(fs_device, s_block[s_idx], zeros);
		  if(s_idx == (INDIRECT_ENTRIES - 1))
			block_write(fs_device, f_block[f_idx], s_block);
		}
		block_write(fs_device, disk_inode->d_indirect, f_block);
	  }
	  block_write(fs_device, sector, disk_inode);
	  success = true;
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->i_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
		  struct inode_disk disk_inode;
		  buffer_cache_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
		  free_inode(&disk_inode);
          free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct inode_disk disk_inode;
  
  lock_acquire(&inode->i_lock);
  buffer_cache_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      buffer_cache_read(sector_idx, buffer, bytes_read, chunk_size, sector_ofs);      

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  lock_release(&inode->i_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk disk_inode;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire(&inode->i_lock);
  buffer_cache_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
  if(disk_inode.length < offset + size){
	if(!grow_inode_disk(&disk_inode, offset + size))
	  return 0;
	buffer_cache_write(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
  }
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&disk_inode, offset);
	  int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;	

	  buffer_cache_write(sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
   lock_release(&inode->i_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk disk_inode;
  buffer_cache_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
  return disk_inode.length;
}

void free_inode(struct inode_disk *disk_inode){
  int i;

  for(i = 0; i < DIRECT_ENTRIES; i++){
	if(!disk_inode->direct[i])
	  return ;
	free_map_release(disk_inode->direct[i], 1);
  }
  if(!disk_inode->indirect)
    return ;
  block_read(fs_device, disk_inode->indirect, f_block);
  for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES; i++){
    if(f_block[i - DIRECT_ENTRIES])
	  return ;
	free_map_release(f_block[i - DIRECT_ENTRIES], 1);
  }
  free_map_release(disk_inode->indirect, 1);
  if(!disk_inode->d_indirect)
	return ;
  block_read(fs_device, disk_inode->d_indirect, f_block);
  for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES * (INDIRECT_ENTRIES + 1); i++){
    block_sector_t f_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
    block_sector_t s_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
    if(s_idx == 0){
	  if(!f_block[f_idx])
		return ;
	  block_read(fs_device, f_block[f_idx], s_block);
	}
    if(!s_block[s_idx])
	  return ;
	free_map_release(s_block[s_idx], 1);
	if(s_idx == INDIRECT_ENTRIES - 1)
	  free_map_release(f_block[f_idx], 1);
  }
}

bool grow_inode_disk(struct inode_disk *disk_inode, off_t length){
  size_t sectors = bytes_to_sectors(length);
  int i = bytes_to_sectors(disk_inode->length);

  for(i = 0; i < DIRECT_ENTRIES && sectors - i > 0; i++){
	if(!disk_inode->direct[i]){
	  if(!free_map_allocate(1, &disk_inode->direct[i]))
		return false;
	  block_write(fs_device, disk_inode->direct[i], zeros);
	}
  }
  if(sectors >= DIRECT_ENTRIES){
	if(!disk_inode->indirect){
	  if(!free_map_allocate(1, &disk_inode->indirect))
	 	 return false;
	  memset(f_block, 0, BLOCK_SECTOR_SIZE);
	}
	else{
	  block_read(fs_device, disk_inode->indirect, f_block);
	}

	for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES && sectors - i > 0; i++){
	  if(!f_block[i - DIRECT_ENTRIES]){
		if(!free_map_allocate(1, &f_block[i - DIRECT_ENTRIES]))
		  return false;
		block_write(fs_device, f_block[i - DIRECT_ENTRIES], zeros);
	  }
	}
	block_write(fs_device, disk_inode->indirect, f_block);
  }
  if(sectors >= DIRECT_ENTRIES + INDIRECT_ENTRIES){
	if(!disk_inode->d_indirect){
	  if(!free_map_allocate(1, &disk_inode->d_indirect))
		return false;
	  memset(f_block, 0, BLOCK_SECTOR_SIZE);
	}
	else{
	  block_read(fs_device, disk_inode->d_indirect, f_block);
	}

	for(;i < DIRECT_ENTRIES + INDIRECT_ENTRIES * (INDIRECT_ENTRIES + 1) && sectors - i > 0; i++){
	  block_sector_t f_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
	  block_sector_t s_idx = (i - DIRECT_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
	  if(s_idx == 0){
		if(!f_block[f_idx]){
		  if(!free_map_allocate(1, &f_block[f_idx]))
			return false;
		  memset(s_block, 0, BLOCK_SECTOR_SIZE);
		}
		else{
		  block_read(fs_device, f_block[f_idx], s_block);
		}
	  }
	  if(!s_block[s_idx]){
		if(!free_map_allocate(1, &s_block[s_idx]))
		  return false;
		block_write(fs_device, s_block[s_idx], zeros);
	  }
	  if(s_idx == (INDIRECT_ENTRIES - 1))
		block_write(fs_device, f_block[f_idx], s_block);
	}
	block_write(fs_device, disk_inode->d_indirect, f_block);
  }
  disk_inode->length = length;
  return true;
}
bool inode_isdir(const struct inode *inode){
  struct inode_disk disk_inode;
  if(inode->removed)
	return false;
  if(!buffer_cache_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0))
	return false;
  return disk_inode.is_dir;
}
