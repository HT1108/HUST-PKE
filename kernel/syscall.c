/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "spike_interface/spike_utils.h"

uint64 block_num;
memblock block[128];
uint64 busy_block_num;
memblock busy_block[128];

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page(uint64 size) {
  // void* pa = alloc_page();
  // uint64 va = g_ufree_page;
  // g_ufree_page += PGSIZE;
  // user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
  //        prot_to_type(PROT_WRITE | PROT_READ, 1));

  // 首次调用malloc，此时没有已映射的页面
  uint64 va = 0;
  if (block_num == 0) {
    void* pa = alloc_page();
    va = g_ufree_page;
    g_ufree_page += PGSIZE;
    user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
      prot_to_type(PROT_WRITE | PROT_READ, 1));
    
    block[0].start = va + size;
    block[0].size = PGSIZE - size;
    block[0].end = va + PGSIZE - 1;
    block_num = 1;

    busy_block[0].start = va;
    busy_block[0].end = va + size - 1;
    busy_block[0].size = size;
    busy_block_num++;
    return va;
  }

  // 查找大小合适的块
  int blockindex = 0;
  for (blockindex = 0; blockindex < block_num; blockindex++) {
    if (block[blockindex].size >= size) {
      va = block[blockindex].start;
      memblock newbusyblock;

      newbusyblock.start = block[blockindex].start;
      newbusyblock.size = size;
      newbusyblock.end = block[blockindex].start + size - 1;
      // 将新的占用块插入数组
      for (int i = busy_block_num; i > 0; i--) {
        if (busy_block[i].start > newbusyblock.start) {
          busy_block[i] = busy_block[i - 1];
        }
        else {
          busy_block[i] = newbusyblock;
        }
      }
      busy_block_num++;

      // 申请大小恰好等于空闲块大小时无需增加新的空闲块，直接break
      if (block[blockindex].size == size) break;

      memblock newblock;
      newblock.start = block[blockindex].start + size;
      newblock.size = block[blockindex].size - size;
      newblock.end = block[blockindex].end;
      // 删除这个块,后面的块向前移动
      for (int i = blockindex; i < block_num - 1; i++) {
        block[i] = block[i + 1];
      }
      block_num--;
      // 将新的空闲块插入数组
      for (int i = block_num; i >= 0; i--) {
        if (i == 0) {
          block[i] = newblock;
          break;
        }
        if (block[i - 1].size >= newblock.size) {
          block[i] = block[i - 1];
        }
        else {
          block[i] = newblock;
        }
      }
      block_num++;
      break;
    }
    
  }

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  // user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);

  // 在busy_block数组中查找va对应的块
  int blockindex;
  memblock newfreeblock;
  for (blockindex = 0; blockindex < busy_block_num; blockindex++) {
    if (busy_block[blockindex].start == va) {
      newfreeblock = busy_block[blockindex];
      // 在block数组中插入空闲块
      for (int i = block_num; i >= 0; i--) {
        if (i == 0) {
          block[i] = newfreeblock;
          break;
        }
        if (block[i - 1].size >= newfreeblock.size) {
          block[i] = block[i - 1];
        }
        else {
          block[i] = newfreeblock;
        }
      }
      block_num++;
      // 删除free的块
      for (int i = blockindex; i < busy_block_num - 1; i++) {
        busy_block[i] = busy_block[i + 1];
      }

      break;
    }
  }
  if (blockindex == busy_block_num) {
    panic("free failed");
  }

  // 在空闲块数组中查找是否有块可以合并
  bool front = FALSE, rear = FALSE;
  uint64 frontindex = 0, rearindex = 0;
  for (int i = 0; i < block_num; i++) {
    if (newfreeblock.start == block[i].end + 1) {
      front = TRUE;
      frontindex = i;
    }
    if (newfreeblock.end == block[i].start - 1) {
      rear = TRUE;
      rearindex = i;
    }
  }
  memblock mergeblock;
  if (front && !rear) {
    mergeblock.start = block[frontindex].start;
    mergeblock.size = block[frontindex].size + newfreeblock.size;
    mergeblock.end = newfreeblock.end;
    for (int i = frontindex; i < block_num - 2; i++) {
      block[i] = block[i + 2];
    }
    block_num -= 2;
  }
  else if (!front && rear) {
    mergeblock.start = newfreeblock.start;
    mergeblock.size = block[rearindex].size + newfreeblock.size;
    mergeblock.end = block[rearindex].end;
    for (int i = rearindex - 1; i < block_num - 2; i++) {
      block[i] = block[i + 2];
    }
    block_num -= 2;
  }
  else if (front && rear) {
    mergeblock.start = block[frontindex].start;
    mergeblock.end = block[rearindex].end;
    mergeblock.size = block[frontindex].size + newfreeblock.size + block[rearindex].size;
    for (int i = frontindex; i < block_num - 2; i++) {
      block[i] = block[i + 3];
    }
    block_num -= 3;
  }

  for (int i = block_num; i >= 0; i--) {
    if (i == 0) {
      block[i] = mergeblock;
      break;
    }
    if (block[i - 1].size >= mergeblock.size) {
      block[i] = block[i - 1];
    }
    else {
      block[i] = mergeblock;
    }
  }
  block_num++;
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page(a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
