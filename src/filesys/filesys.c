#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>

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

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  /*set initial thread to root directory*/
  thread_current ()->cur_dir = dir_open_root ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* A helper to traverse through a directory path of name
returning the final name of the actual file on the path on success
and returning NULL on failure.
Also makes sure to set directory to the final directory
of the path.*/
char *
traverse_path (const char *name, struct dir **directory)
{
  struct dir *cur_dir;

  if (name == NULL)
    {
      *directory = NULL;
      return NULL;
    }

  /*get the directory path from the name*/
  /*if its an absolute path start at the root directory*/
  if (name[0] == '/')
    {
      cur_dir = dir_open_root ();
    }
  else
    {
      cur_dir = dir_reopen (thread_current ()->cur_dir);
    }

  int length_of_name = strlen (name);

  char s[length_of_name + 1];
  strlcpy (s, name, length_of_name + 1);

  char *token, *save_ptr;
  token = strtok_r (s, "/", &save_ptr);

  if (token == NULL)
    {
      *directory = NULL;
      dir_close (cur_dir);
      return NULL;
    }

  /*go to the directory based on the path*/
  while (token != NULL)
    {
      char *next_token = strtok_r (NULL, "/", &save_ptr);

      /*if the next_token is null then current token is the actual entry_name
      i.e for name = a/b/c/d  d is the entry_name so break out of loop
      as current token is entry_name*/
      if (next_token == NULL)
        {
          break;
        }

      /*else continue the directory path. token is the name of next directory
      to go to*/
      struct inode *next_dir_inode;

      /*look up the next directory*/
      bool success = dir_lookup (cur_dir, token, &next_dir_inode);

      if (success == false)
        {
          *directory = NULL;
          dir_close (cur_dir);
          return NULL;
        }

      dir_close (cur_dir);

      /*go to next directory*/
      cur_dir = dir_open (next_dir_inode);

      if (cur_dir == NULL)
        {
          *directory = NULL;
          inode_close (next_dir_inode);
          return NULL;
        }

      token = next_token;
    }

  *directory = cur_dir;

  char *entry_name = malloc (strlen (token) + 1);
  if (entry_name == NULL)
    {
      *directory = NULL;
      dir_close (cur_dir);
      return NULL;
    }

  /*the last token after breaking out of loop is the entry_name*/
  strlcpy (entry_name, token, strlen (token) + 1);

  return entry_name;
}

/* Creates a dir named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a dir named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_dir_create (const char *name)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;

  char *entry_name = traverse_path (name, &dir);

  if (dir == NULL)
    {
      free (entry_name);
      return false;
    }

  block_sector_t parent_sector = dir->inode->sector;

  bool success = (free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 2, parent_sector)
                  && dir_add (dir, entry_name, inode_sector));

  if (!success && inode_sector != 0)
    {
      free_map_release (inode_sector, 1);
    }

  dir_close (dir);
  free (entry_name);
  return success;
}

/* Creates a file  named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir;

  /*check if the current directory is "removed" and if it is
  then attempts to create newfiles
  in a deleted directory must be disallowed so return null.
  (first need to make sure its not an absolute path)*/
  if (name[0] != '/' && thread_current ()->cur_dir->inode->removed == true)
    {
      return false;
    }

  char *entry_name = traverse_path (name, &dir);

  // bool success = (dir != NULL && free_map_allocate (1, &inode_sector)
  //                 && inode_create (inode_sector, initial_size, 0)
  //                 && dir_add (dir, entry_name, inode_sector));

  bool success = false;

  if (dir != NULL && free_map_allocate (1, &inode_sector)
      && inode_create (inode_sector, initial_size, 0))
    {
      // printf ("FILESYS_CREATE: name='%s', inode_sector=%u,
      // parent_sector=%u\n",
      //         entry_name, inode_sector, dir->inode->sector);

      success = dir_add (dir, entry_name, inode_sector);
    }
  if (!success && inode_sector != 0)
    {
      free_map_release (inode_sector, 1);
    }

  dir_close (dir);
  free (entry_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or
   a null pointer otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;
  struct inode *inode = NULL;

  /*special case for opening root directory "/"*/
  if (strcmp (name, "/") == 0)
    {

      dir = dir_open_root ();
      if (dir != NULL)
        {
          inode = inode_reopen (dir->inode);
          dir_close (dir);
        }
      return file_open (inode);
    }

  /*check if the current directory is "removed" and if it is
  then attempts to open files (including . and ..)
  in a deleted directory must be disallowed so return null.
  (first need to make sure its not an absolute path)*/
  if (name[0] != '/' && thread_current ()->cur_dir->inode->removed == true)
    {
      return NULL;
    }

  char *entry_name = traverse_path (name, &dir);

  if (dir != NULL)
    dir_lookup (dir, entry_name, &inode);

  dir_close (dir);
  free (entry_name);

  /*check if inode isn't "removed" before opening it*/
  if (inode != NULL && inode->removed == true)
    {
      inode_close (inode);
      return NULL;
    }

  return file_open (inode);
}

/* Deletes the file or dir named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir;
  struct inode *inode = NULL;

  char *entry_name = traverse_path (name, &dir);

  if (dir != NULL)
    dir_lookup (dir, entry_name, &inode);

  if (inode == NULL)
    {
      free (entry_name);
      return false;
    }

  /*need to see if the path/thing we are trying to remove
  is a directory entry or not*/

  /* if its a directory u can only remove if its empty
  so we check if its empty. If its not empty we return false
  and don't remove it*/
  if (inode->data.file_or_dir == 1)
    {

      /*open the directory*/
      struct dir *cur_dir = dir_open (inode);

      char name[NAME_MAX + 1];

      /*read all directory entires in cur_dir and store the
      current entires name in name*/
      while (dir_readdir (cur_dir, name) != false)
        {

          /*if the directory has an entry that isn't the special
          . or .. directory entry then the directory isn't empty
          and we cant remove and return false*/
          if (strcmp (name, ".") != 0 && strcmp (name, "..") != 0)
            {
              dir_close (cur_dir);
              inode_close (inode);
              dir_close (dir);
              return false;
            }
        }
      dir_close (cur_dir);
    }

  /*if the above while loop passes without returning false
  then the directory is empty and we can remove the input*/

  /*we only need to check if its a directory since if its a file
  we can just remove it*/
  bool success = dir_remove (dir, entry_name);
  inode_close (inode);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
