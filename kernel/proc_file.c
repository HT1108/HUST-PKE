/*
 * Interface functions between file system and kernel/processes. added @lab4_1
 */

#include "proc_file.h"

#include "hostfs.h"
#include "pmm.h"
#include "vmm.h"
#include "process.h"
#include "ramdev.h"
#include "rfs.h"
#include "riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "util/string.h"
void resolve_relative_path(char* dest, char* relapath) {
  char* cwd = current->cwd;
  int relapath_len = strlen(relapath);
  int cwd_len = strlen(cwd);
  // ./开头

  if (relapath[1] == '/') {
    
    strcpy(dest, cwd);
    int j = strlen(dest);
    
    int i;

    for (i = 1; relapath[i] != 0; i++) {
      if (j == 1) {
        dest[j + i - 2] = relapath[i];
      }
      else {
        dest[j + i - 1] = relapath[i];
      }
      
    }
  }
  else if (relapath[1] == '.') {//  ../开头
    int flag = FALSE;// 是否找到最后一个'/'
    for (int i = cwd_len; i >= 0; i--) { //倒序找到最后一个'/',把之前的路径复制到dest
      dest[i] = '\0';
      if (flag) {
        dest[i] = cwd[i];
      }
      if (cwd[i - 1] == '/') {
        flag = TRUE;
      }
    }
    int i = strlen(dest);
    int j = 2;
    while (relapath[j] != 0) {
      if (j == 2) {
        dest[i] = '/';
      }
      else {
        dest[i] = relapath[j];
      }


      i++, j++;
    }
  }
  else {
    return;
  }


}
//
// initialize file system
//
void fs_init(void) {
  // initialize the vfs
  vfs_init();

  // register hostfs and mount it as the root
  if (register_hostfs() < 0) panic("fs_init: cannot register hostfs.\n");
  struct device* hostdev = init_host_device("HOSTDEV");
  vfs_mount("HOSTDEV", MOUNT_AS_ROOT);

  // register and mount rfs
  if (register_rfs() < 0) panic("fs_init: cannot register rfs.\n");
  struct device* ramdisk0 = init_rfs_device("RAMDISK0");
  rfs_format_dev(ramdisk0);
  vfs_mount("RAMDISK0", MOUNT_DEFAULT);
}

//
// initialize a proc_file_management data structure for a process.
// return the pointer to the page containing the data structure.
//
proc_file_management* init_proc_file_management(void) {
  proc_file_management* pfiles = (proc_file_management*)alloc_page();
  pfiles->cwd = vfs_root_dentry; // by default, cwd is the root
  pfiles->nfiles = 0;

  for (int fd = 0; fd < MAX_FILES; ++fd)
    pfiles->opened_files[fd].status = FD_NONE;

  sprint("FS: created a file management struct for a process.\n");
  return pfiles;
}

//
// reclaim the open-file management data structure of a process.
// note: this function is not used as PKE does not actually reclaim a process.
//
void reclaim_proc_file_management(proc_file_management* pfiles) {
  free_page(pfiles);
  return;
}

//
// get an opened file from proc->opened_file array.
// return: the pointer to the opened file structure.
//
struct file* get_opened_file(int fd) {
  struct file* pfile = NULL;

  // browse opened file list to locate the fd
  for (int i = 0; i < MAX_FILES; ++i) {
    pfile = &(current->pfiles->opened_files[i]);  // file entry
    if (i == fd) break;
  }
  if (pfile == NULL) panic("do_read: invalid fd!\n");
  return pfile;
}

//
// open a file named as "pathname" with the permission of "flags".
// return: -1 on failure; non-zero file-descriptor on success.
//
int do_open(char* pathname, int flags) {
  struct file* opened_file = NULL;

  // if (pathname[0] == '.') {// relative path
  //   char path_resolved[MAX_PATH_LEN];

  //   resolve_relative_path(path_resolved, pathname);
  //   char* resolvedpath_pa = (char*)user_va_to_pa(current->pagetable, path_resolved);

  //   if ((opened_file = vfs_open(resolvedpath_pa, flags)) == NULL) return -1;
  // }
  // else {
  //   if ((opened_file = vfs_open(pathname, flags)) == NULL) return -1;
  // }
  if ((opened_file = vfs_open(pathname, flags)) == NULL) return -1;
  int fd = 0;
  if (current->pfiles->nfiles >= MAX_FILES) {
    panic("do_open: no file entry for current process!\n");
  }
  struct file* pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read content of a file ("fd") into "buf" for "count".
// return: actual length of data read from the file.
//
int do_read(int fd, char* buf, uint64 count) {
  struct file* pfile = get_opened_file(fd);

  if (pfile->readable == 0) panic("do_read: no readable file!\n");

  char buffer[count + 1];
  int len = vfs_read(pfile, buffer, count);
  buffer[count] = '\0';
  strcpy(buf, buffer);
  return len;
}

//
// write content ("buf") whose length is "count" to a file "fd".
// return: actual length of data written to the file.
//
int do_write(int fd, char* buf, uint64 count) {
  struct file* pfile = get_opened_file(fd);

  if (pfile->writable == 0) panic("do_write: cannot write file!\n");

  int len = vfs_write(pfile, buf, count);
  return len;
}

//
// reposition the file offset
//
int do_lseek(int fd, int offset, int whence) {
  struct file* pfile = get_opened_file(fd);
  return vfs_lseek(pfile, offset, whence);
}

//
// read the vinode information
//
int do_stat(int fd, struct istat* istat) {
  struct file* pfile = get_opened_file(fd);
  return vfs_stat(pfile, istat);
}

//
// read the inode information on the disk
//
int do_disk_stat(int fd, struct istat* istat) {
  struct file* pfile = get_opened_file(fd);
  return vfs_disk_stat(pfile, istat);
}

//
// close a file
//
int do_close(int fd) {
  struct file* pfile = get_opened_file(fd);
  return vfs_close(pfile);
}

//
// open a directory
// return: the fd of the directory file
//
int do_opendir(char* pathname) {
  struct file* opened_file = NULL;
  if ((opened_file = vfs_opendir(pathname)) == NULL) return -1;

  int fd = 0;
  struct file* pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }
  if (pfile->status != FD_NONE)  // no free entry
    panic("do_opendir: no file entry for current process!\n");

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read a directory entry
//
int do_readdir(int fd, struct dir* dir) {
  struct file* pfile = get_opened_file(fd);
  return vfs_readdir(pfile, dir);
}

//
// make a new directory
//
int do_mkdir(char* pathname) {
  return vfs_mkdir(pathname);
}

//
// close a directory
//
int do_closedir(int fd) {
  struct file* pfile = get_opened_file(fd);
  return vfs_closedir(pfile);
}

//
// create hard link to a file
//
int do_link(char* oldpath, char* newpath) {
  return vfs_link(oldpath, newpath);
}

//
// remove a hard link to a file
//
int do_unlink(char* path) {
  return vfs_unlink(path);
}




int do_rcwd(char* path) {

  memcpy(path, current->cwd, MAX_PATH_LEN);

  return 0;
}

int do_ccwd(char* path) {
  char path_resolved[MAX_PATH_LEN];
  memset(path_resolved, 0, MAX_PATH_LEN);
  if (path[0] == '.') {
    resolve_relative_path(path_resolved, path);
    memcpy(current->cwd, path_resolved, MAX_PATH_LEN);
  }
  else {
    memcpy(current->cwd, path, MAX_PATH_LEN);
  }

  return 0;
}

