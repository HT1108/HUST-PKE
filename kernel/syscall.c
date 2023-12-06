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
#include "elf.h"


#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
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

// copy from elf.c
static size_t parse_args(arg_buf* arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
    sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64* pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char*)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

// copy from elf.c
static uint64 elf_fpread(elf_ctx* ctx, void* dest, uint64 nb, uint64 offset) {
  elf_info* msg = (elf_info*)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
}

ssize_t sys_user_printbacktrace(uint64 layer) {
  uint64 userStackTop = current->trapframe->kernel_sp;
  elf_ctx* elf = NULL;
  arg_buf arg;
  int argc = parse_args(&arg);
  elf_shtbl sectiontable[50];
  elf_ctx elfloader;
  elf_info info;
  elf_sym sym_tab[50];
  info.f = spike_file_open(arg.argv[0], O_RDONLY, 0);
  info.p = current;
  char strnbuf[500] = "";
  
  elf_init(&elfloader, &info);// 读取elf头
  
  //读取section header
  elf_fpread(&elfloader, sectiontable, elfloader.ehdr.shentsize * elfloader.ehdr.shnum, elfloader.ehdr.shoff);

  int symnum = 0;// num of symbols
  // 在section header table中查找符号表地址
  for (int i = 0; i < elfloader.ehdr.shnum; i++) {
    if (sectiontable[i].sh_type == SHT_SYMTAB) {
      elf_fpread(&elfloader, sym_tab, sectiontable[i].sh_size, sectiontable[i].sh_offset);
      symnum = sectiontable[i].sh_size / sectiontable[i].sh_entsize;  // 计算符号数量
    }
  }
  // 读取string table
  for (int i = 0; i < elfloader.ehdr.shnum; i++) {
    // elf有两个 type == 3 的 section，其中 shstrndx 指向的是section name，不是函数名表
    if (sectiontable[i].sh_type == 3 && i != elfloader.ehdr.shstrndx) {
      elf_fpread(&elfloader, strnbuf, sectiontable[i].sh_size, sectiontable[i].sh_offset);
    }
  }
  

  
  
  int nowlayer = layer;
  uint64 ra = 0;
  uint64 fp = current->trapframe->regs.s0;// 当前函数的fp
  uint64 savedfp = *(uint64*)(fp - 8);;// 原fp
  while (nowlayer > 0) {
    ra = *(uint64*)(savedfp - 8);
    // 查找ra位于哪一个函数
    for (int i = 0; i < symnum; i++) {
      if (ra >= sym_tab[i].st_value && ra <= (sym_tab[i].st_value + sym_tab[i].st_size) && strcmp(strnbuf + sym_tab[i].st_name, "main") == 0) {
        return 0;
      }
      if (ra >= sym_tab[i].st_value && ra <= sym_tab[i].st_value + sym_tab[i].st_size) {
        sprint("%s\n", strnbuf + sym_tab[i].st_name);
      }
    }
    savedfp = *(uint64*)(savedfp - 16);
    nowlayer--;
  }

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
    case SYS_user_printbacktrace:
      return sys_user_printbacktrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
