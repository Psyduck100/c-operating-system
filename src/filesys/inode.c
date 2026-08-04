#include "filesys/inode.h"
#include "threads/synch.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "devices/block.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define NUM_DIRECT 124
#define PTRS_PER_SECTOR 128
#define NOT_ALLOCATED ((block_sector_t) -1)
typedef uint32_t block_sector_t;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[NUM_DIRECT];  /* direct pointer to disk data sectors */
    block_sector_t indirect;            /* indirect sector containing direct data sectors*/
    block_sector_t double_indirect;     /* double indirect sector containing indirect_direct sectors*/
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
    struct inode_disk data;             /* Inode content. */
    struct lock inode_lock;            /* Lock for extending the inode. */
  };

  struct indirect_block
  {
    block_sector_t sectors[PTRS_PER_SECTOR]; /*the pts to the actual in disk blocks*/
  };


/* Returns the block device sector that contains byte offset POS
   within INODE.
   If alloc is true then this function will also return a new
   sector to use even if the INODE does not contain data for a
   byte at offset.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. Modifies the cached on disk inode in *inode so needs to be
   written back after allocation*/
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos, bool alloc) 
{
  ASSERT (inode != NULL);

  //this is what sector our position WOULD'VE been in if conitguous
  block_sector_t sector_index = pos / BLOCK_SECTOR_SIZE;
  struct inode_disk *on_disk_inode = &inode->data;

  /*CASE 1*/

  /*If our sector_index is smaller than the number of direct block pointers
  then we didnt need indirect so it lives right in the direct ptr array*/
  if (sector_index < NUM_DIRECT){
    /*if the ptr is 0 then its unallocated so we need to handle that*/
    if (on_disk_inode->direct[sector_index] == 0) {
      if (!alloc) {
        return NOT_ALLOCATED;
      }
      block_sector_t allocated_sector;
      if (!free_map_allocate (1, &allocated_sector)) {
        return NOT_ALLOCATED;
      }
      /*create block of zeros and write that to allocaed sector*/
      char zeros[BLOCK_SECTOR_SIZE] = {0};
      block_write(fs_device, allocated_sector, zeros);
      on_disk_inode->direct[sector_index] = allocated_sector;
    }


    
    return on_disk_inode->direct[sector_index];
  }
    

  /*if not we need to find the actual sector within the indirect,
  subtract NUM_DIRECT to measure how far into indirects we are*/
  sector_index -= NUM_DIRECT;

  /*CASE 2*/

  /*If its sector_index is smaller than the initial PTRS_PER_SECTOR 
  then its in the indirect pointers*/
  if (sector_index < PTRS_PER_SECTOR) {

    /*if indirect pointer */
    if (on_disk_inode->indirect == 0) {
      if (!alloc){
        return NOT_ALLOCATED;
      }
  
      if (!free_map_allocate(1, &on_disk_inode->indirect)){
        return NOT_ALLOCATED
      }

      char zeros[BLOCK_SECTOR_SIZE] = {0};
      block_write(fs_device, on_disk_inode->indirect, zeros);
    }


    // need to access indirect block's blocks memory address somehow
    struct indirect_block ib;
    block_read (fs_device, on_disk_inode->indirect, &ib);
    
    block_sector_t s = ib.sectors[sector_index];

    if (s == 0){
      if (!alloc){
        return NOT_ALLOCATED;
      }
      block_sector_t allocated_sector;
      if (!free_map_allocate(1, &allocated_sector)){
        return NOT_ALLOCATED;
      }
      char zeros[BLOCK_SECTOR_SIZE] = {0};
      block_write(fs_device, allocated_sector, zeros);
      ib.sectors[sector_index] = allocated_sector;
      block_write(fs_device, on_disk_inode->indirect, &ib);
      
      
    }
    return ib.sectors[sector_index];
  }

  /*CASE 3*/
  /*otherwise the actual sector addr lies within the double indirect*/
  sector_index -= PTRS_PER_SECTOR;

  /*just make sure that we are within bounds of filesize*/
  ASSERT (sector_index < PTRS_PER_SECTOR * PTRS_PER_SECTOR);

  /*index of the wanted indirect block within doubly*/
  block_sector_t ib_index = sector_index / PTRS_PER_SECTOR;
  /*index of the wanted direct block within indirect*/
  block_sector_t direct_index = sector_index % PTRS_PER_SECTOR;

  /*check if not allocated, then attempt to allocate block for doubly indirect layer*/
  if (on_disk_inode->double_indirect == 0)
  {
    if (!alloc){
      return NOT_ALLOCATED;
    }
    if(!free_map_allocate(1, &on_disk_inode->double_indirect)){
      return NOT_ALLOCATED;
    }
    char zeros[BLOCK_SECTOR_SIZE] = {0};
    
    block_write(fs_device, on_disk_inode->double_indirect, zeros);


  }



  /*find the address of the indirect block within the doubly*/
  struct indirect_block d_ib;
  block_read(fs_device, on_disk_inode->double_indirect, &d_ib); 
  block_sector_t ib_sector_addr = d_ib.sectors[ib_index];

  /*if indirect block ptr within the doubly indirect block is zero
  attempt an alloc*/
  if (ib_sector_addr == 0){

    if (!alloc){
      return NOT_ALLOCATED;
    }
    if (!free_map_allocate(1, &d_ib.sectors[ib_index])){
      return NOT_ALLOCATED;
    }
    char zeros[BLOCK_SECTOR_SIZE] = {0};
    /*zero out the indirect block's sector*/
    block_write(fs_device, d_ib.sectors[ib_index], zeros);

    block_write(fs_device, on_disk_inode->double_indirect, &d_ib);



  }





  /*find the address of the direct block within the indirect*/
  struct indirect_block ib; 
  block_read(fs_device, d_ib.sectors[ib_index], &ib);
  block_sector_t direct_sector_addr = ib.sectors[direct_index];

  /*once again, if direct block ptr within the indirect is zero
  we attempt to allocate a block for it*/
  if (direct_sector_addr == 0){
    if(!alloc){
      return NOT_ALLOCATED;
    }
    block_sector_t new_sector;
    if (!free_map_allocate(1, &new_sector)){
      return NOT_ALLOCATED;
    }
    char zeros[BLOCK_SECTOR_SIZE] = {0};
    block_write(fs_device, new_sector, zeros);
    ib.sectors[direct_index] = new_sector;
    /*here we complete it by writing the indirect block sector
    to the doubly indirect's sector*/
    block_write(fs_device, d_ib.sectors[ib_index], &ib);
  }


  return ib.sectors[direct_index];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      if (sectors > 0) 
        {
          char zeros[BLOCK_SECTOR_SIZE] = {0};

          /*allocate sectors to fill up the direct data blocks first*/
          for (int i = 0; i < sectors && i < NUM_DIRECT; i++) {

            //allocate sectors 1 by 1 so they do not have to be contiguous
            if (free_map_allocate (1, &disk_inode->direct[i])){
              block_write(fs_device, disk_inode->direct[i], zeros);
            }
            else{
              return false;
            }
          }

          /*if number of sectors is greater than what our direct data can hold 
          fill up indirect data. Our one indirect data pointer can hold 128 
          direct data sectors*/
          if (sectors > NUM_DIRECT){
              
            /*allocate sector for indirect data block sector*/
            bool success = free_map_allocate (1, &disk_inode->indirect);
            if (!success) return false;

            /*calloc space for temporary indirect array that will store the direct sectors*/
            block_sector_t *indirect_sector_content = calloc(PTRS_PER_SECTOR, sizeof(block_sector_t));
            if (indirect_sector_content == NULL){
              return false;
            }
            
            /*allocate sectors for indirect_sector_content
            NUM_DIRECT + PTRS_PER_SECTOR is the total number of sectors that can be allocated
            from direct blocks and an indirect block.*/
            for (int i = NUM_DIRECT; i < sectors && i < NUM_DIRECT + PTRS_PER_SECTOR; i++){

              /*allocate sectors 1 by 1 to indirect_sector_content so they do not have to be contiguous*/
              if (free_map_allocate (1, &indirect_sector_content[i-NUM_DIRECT])){
                block_write(fs_device, indirect_sector_content[i-NUM_DIRECT], zeros);
              }
              else{
                return false;
              }
              
            }

            /*after indirect_sector_content is done allocating sectors we write the content
            into the sector that contains the indirect content*/
            block_write(fs_device, disk_inode->indirect, indirect_sector_content);
            
            free(indirect_sector_content);
          }

          /*if we need more sectors than direct and indirect can fill use doubly indirect*/
          if (sectors > NUM_DIRECT + PTRS_PER_SECTOR){
            
            /*allocate sector for doubly indirect data block sector*/
            bool success = free_map_allocate (1, &disk_inode->double_indirect);
            if (!success) return false;

             /*calloc space for temporary double indirect array that will store the indirect sectors*/
            block_sector_t *double_indirect_content = calloc(PTRS_PER_SECTOR, sizeof(block_sector_t));
            if (double_indirect_content == NULL){
                return false;
            }

            bool sectors_not_done = true;
            size_t cur_indirect_sector = 0;

            while (sectors_not_done)
            {

              /*allocate a indirect sector*/
              bool success = free_map_allocate (1, &double_indirect_content[cur_indirect_sector]);
              if (!success) return false;

              /*calloc space for temporary indirect array that will store the direct sectors*/
              block_sector_t *indirect_sector_content = calloc(PTRS_PER_SECTOR, sizeof(block_sector_t));
              if (indirect_sector_content == NULL){
                return false;
              }
              
              /*find out the current starting sector for our double indirect block
              it would be our direct sector (NUM_DIRECT) + our indirect block (PTRS_PERSECTOR) 
              + our current indirect sector for our doubly indirect sector 
              (PTRS_PERSECTOR * cur_indirect_sector) */
              size_t cur_starting_sector = NUM_DIRECT + PTRS_PER_SECTOR + PTRS_PER_SECTOR * cur_indirect_sector;

              /*allocate sectors for indirect_sector_content*/
              for (int i = cur_starting_sector; i < sectors && i < cur_starting_sector + PTRS_PER_SECTOR; i++){

                /*allocate sectors 1 by 1 to indirect_sector_content so they do not have to be contiguous*/
                if (free_map_allocate (1, &indirect_sector_content[i-cur_starting_sector])){
                  block_write(fs_device, indirect_sector_content[i-cur_starting_sector], zeros);
                }
                else{
                  return false;
                }
              
              }

              /*if all our sectors are allocated then we end the loop here for our double indirect sector*/
              if (sectors < cur_starting_sector + PTRS_PER_SECTOR){
                sectors_not_done = false;
              }

              /*after indirect_sector_content is done allocating sectors we write the content
              into the sector that contains the indirect content*/
              block_write(fs_device, double_indirect_content[cur_indirect_sector], indirect_sector_content);
            
              free(indirect_sector_content);


              cur_indirect_sector++;

            }

              /*after double_indirect_sector_content is done allocating sectors we write the content
              into the sector that contains the double indirect content*/
              block_write(fs_device, disk_inode->double_indirect, double_indirect_content);
            
              free(double_indirect_content);
          }
        }

      /*after all frame_allocation write disk_inode to sector*/
      block_write (fs_device, sector, disk_inode);
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
  lock_init(&inode->inode_lock);
  block_read (fs_device, inode->sector, &inode->data);
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
          /* Free the inode sector */
          free_map_release (inode->sector, 1);
          /* Need to deallocate all data blocks */
          off_t num_blocks = bytes_to_sectors(inode->data.length);
          for (int i = 0; i < num_blocks; i++) {
            block_sector_t sector = byte_to_sector(inode, i * BLOCK_SECTOR_SIZE);
            if (sector != 0) {
              free_map_release(sector, 1);
            }
          }
          
          /* Need to deallocate the single indirect block*/
          if (inode->data.indirect != 0) {
            free_map_release(inode->data.indirect, 1);
          }

          /* Need to deallocate the double indirect block and all sub-blocks*/
          if (inode->data.double_indirect != 0) {
              block_sector_t double_indirect_sector[PTRS_PER_SECTOR];
              block_read(fs_device, inode->data.double_indirect, double_indirect_sector);
              for (int i = 0; i < PTRS_PER_SECTOR; i++) {
                  if (double_indirect_sector[i] != 0) {
                      free_map_release(double_indirect_sector[i], 1);
                  }
                }
              free_map_release(inode->data.double_indirect, 1);
            }
          

          
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
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
// off_t
// inode_write_at (struct inode *inode, const void *buffer_, off_t size,
//                 off_t offset) 
// {
//   const uint8_t *buffer = buffer_;
//   off_t bytes_written = 0;
//   uint8_t *bounce = NULL;

//   if (inode->deny_write_cnt)
//     return 0;

  
//   /* Want to extend file size if necessary*/
//   if (offset + size > inode_length(inode)) {

//     /* Acquire lock to avoid race conditions */
//     lock_acquire(&inode->inode_lock);

    
//     off_t new_length = offset + size;
//     off_t old_blocks = bytes_to_sectors(inode_length(inode));
//     off_t new_blocks = bytes_to_sectors(new_length);

//     char zeros[BLOCK_SECTOR_SIZE];
//     memset(zeros, 0, BLOCK_SECTOR_SIZE);
    
//     block_sector_t indirect_content[PTRS_PER_SECTOR];
//     /*if indirect already has a sector*/
//     if (inode->data.indirect != 0){
//       block_read(fs_device, inode->data.indirect, indirect_content);
//     }
//     /*if not allocated and we need space for it allocate it*/
//     else if (new_blocks > NUM_DIRECT){
//       if (!free_map_allocate(1, &inode->data.indirect)) return 0;
//       memset(&indirect_content, 0 , sizeof(indirect_content));
//     }

//     block_sector_t double_indirect_content[PTRS_PER_SECTOR];

//     /*if double_indirect already has a sector*/
//     if (inode->data.double_indirect != 0){
//       block_read(fs_device, inode->data.double_indirect, double_indirect_content);
//     }
//     /*if not allocated and we need space for it allocate it*/
//     else if (new_blocks > NUM_DIRECT + PTRS_PER_SECTOR){
//       if (!free_map_allocate(1, &inode->data.double_indirect)) return 0;
//       memset(&double_indirect_content, 0 , sizeof(double_indirect_content));
//     }

//     /*alloc blocks */
//     for (off_t i = old_blocks; i < new_blocks; i++) {
//       block_sector_t new_sector = byte_to_sector(inode, offset + i * BLOCK_SECTOR_SIZE, true);
//       if (new_sector == -1) {
//         lock_release(&inode->inode_lock);
//         return bytes_written; // Allocation failed, return bytes written so far
//       }
      
//       block_write(fs_device, new_sector, zeros);

//       /* Insert into direct blocks until space runs out */
//       if (i < NUM_DIRECT) {
//         inode->data.direct[i] = new_sector;
//       }
//       /* if need to insert to indirect block */
//       else if (i >= NUM_DIRECT && i < NUM_DIRECT + PTRS_PER_SECTOR){

//         indirect_content[i - NUM_DIRECT] = new_sector;

//       }

//       /*if need to insert in double indirect block*/
//       else if (i >= NUM_DIRECT + PTRS_PER_SECTOR) {

//         /*get the current indirect sector index*/
//         int cur_indirect_sector_index = (i - (NUM_DIRECT + PTRS_PER_SECTOR)) / PTRS_PER_SECTOR;

//         /*read the sector from the double_indirect_content*/
//         block_sector_t cur_indirect_content[PTRS_PER_SECTOR];
//         /*if the current indirect sector is already allocated*/
//         if (double_indirect_content[cur_indirect_sector_index] != 0){
//           block_read(fs_device, double_indirect_content[cur_indirect_sector_index], cur_indirect_content);
//         }
//         /*if not allocated*/
//         else {
//           if (!free_map_allocate(1, &double_indirect_content[cur_indirect_sector_index])) return 0;
//           memset(&cur_indirect_content, 0 , sizeof(cur_indirect_content));
//         }

//         /*update the sector with the new sector and write it back*/
//         cur_indirect_content[(i - (NUM_DIRECT + PTRS_PER_SECTOR)) % PTRS_PER_SECTOR] = new_sector;
//         block_write(fs_device, double_indirect_content[cur_indirect_sector_index] ,cur_indirect_content);

//       }
//     }

//     /*write back indirect and double_indirect after updates*/
//     block_write(fs_device, inode->data.indirect, indirect_content);
//     block_write(fs_device, inode->data.double_indirect, double_indirect_content);

//     inode->data.length = new_length;

//     //rewrite data after changing it
//     block_write(fs_device, inode->sector, &inode->data);
//   }

//   while (size > 0) 
//     {
//       /* Sector to write, starting byte offset within sector. */
//       block_sector_t sector_idx = byte_to_sector (inode, offset);
//       int sector_ofs = offset % BLOCK_SECTOR_SIZE;

//       /* Bytes left in inode, bytes left in sector, lesser of the two. */
//       off_t inode_left = inode_length (inode) - offset;
//       int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
//       int min_left = inode_left < sector_left ? inode_left : sector_left;

//       /* Number of bytes to actually write into this sector. */
//       int chunk_size = size < min_left ? size : min_left;
//       if (chunk_size <= 0)
//         break;

//       if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
//         {
//           /* Write full sector directly to disk. */
//           block_write (fs_device, sector_idx, buffer + bytes_written);
//         }
//       else 
//         {
//           /* We need a bounce buffer. */
//           if (bounce == NULL) 
//             {
//               bounce = malloc (BLOCK_SECTOR_SIZE);
//               if (bounce == NULL)
//                 break;
//             }

//           /* If the sector contains data before or after the chunk
//              we're writing, then we need to read in the sector
//              first.  Otherwise we start with a sector of all zeros. */
//           if (sector_ofs > 0 || chunk_size < sector_left) 
//             block_read (fs_device, sector_idx, bounce);
//           else
//             memset (bounce, 0, BLOCK_SECTOR_SIZE);
//           memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
//           block_write (fs_device, sector_idx, bounce);
//         }

//       /* Advance. */
//       size -= chunk_size;
//       offset += chunk_size;
//       bytes_written += chunk_size;
//     }
//   free (bounce);

//   return bytes_written;
// }

off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  
  /* Want to extend file size if necessary*/
  if (offset + size > inode_length(inode)) {

    /* Acquire lock to avoid race conditions */
    lock_acquire(&inode->inode_lock);

    
    off_t new_length = offset + size;
    off_t old_blocks = bytes_to_sectors(inode_length(inode));
    off_t new_blocks = bytes_to_sectors(new_length);


    /*alloc blocks, casework is already handled by byte_to_sector*/
    off_t i;
    for (i = old_blocks; i < new_blocks; i++) {
      block_sector_t new_sector = byte_to_sector(inode, i * BLOCK_SECTOR_SIZE, true);
      if (new_sector == NOT_ALLOCATED) {
        break;
      }

    }

    /*write back to disk as byte to sector modified on_disk_inode*/
    inode->data.length = (i == new_blocks) ? new_length : i * BLOCK_SECTOR_SIZE;
    block_write(fs_device, inode->sector, &inode->data);
    lock_release(&inode->inode_lock);

  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

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
  return inode->data.length;
}

/* Returns the new block allocated*/
static block_sector_t
alloc_block (void)
{
  block_sector_t sector;
  if (!free_map_allocate (1, &sector))
    return 0;
  return sector;
}