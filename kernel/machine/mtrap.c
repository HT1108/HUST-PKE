#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "spike_interface/spike_file.h"
#include "kernel/elf.h"
#include "util/string.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

typedef struct elf_info_t {
  spike_file_t* f;
  process* p;
} elf_info;

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char* argv[MAX_CMDLINE_ARGS];
} arg_buf;

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
char dbgbuf[16392];
static void handle_illegal_instruction() {
  
  elf_ctx elfloader;
  elf_info info;
  arg_buf arg;
  
  int argc = parse_args(&arg);
  
  info.f = spike_file_open(arg.argv[0], O_RDONLY, 0);
  info.p = current;
  elf_sect_header sectiontable[50];
  elf_init(&elfloader, &info);// 读取elf头
  //读取section header
  elf_fpread(&elfloader, sectiontable, elfloader.ehdr.shentsize * elfloader.ehdr.shnum, elfloader.ehdr.shoff);

  char strnbuf[512] = "";

  int index = elfloader.ehdr.shstrndx;
  elf_fpread(&elfloader, strnbuf, sectiontable[index].size, sectiontable[index].offset);
  
 
  debug_header dbgheader;
  // 查找debug_line段
  for (int i = 1; i < elfloader.ehdr.shnum; i++) {
    if (!strcmp(strnbuf + sectiontable[i].name, ".debug_line")) {
      elf_fpread(&elfloader, dbgbuf, sectiontable[i].size, sectiontable[i].offset);
      elf_fpread(&elfloader, &dbgheader, sizeof(debug_header), sectiontable[i].offset);// 读取debug_line header
      make_addr_line(&elfloader, dbgbuf, sectiontable[i].size);
      break;
    }
  }

  uint64 breakpoint = read_csr(mepc); // 读取断点指令地址 machine exception program counter
  int linenum = 0;
  char* filename = NULL;
  char* dir = NULL;
  for (int i = 0; i < current->line_ind; i++) {
    if (breakpoint == current->line[i].addr) {
      linenum = current->line[i].line;
      filename = current->file[current->line[i].file].file;
      dir = current->dir[current->file[current->line[i].file].dir];
      sprint("Runtime error at %s/%s:%d\n", dir, filename, linenum);
      //break;
    }
  }
  char path[50];
  
  size_t dir_len = strlen((const char*)dir);
  int filename_len = strlen((const char*)filename);
  strcpy(path, (const char*)dir);
  path[dir_len] = '/';
  strcpy(path + dir_len + 1, (const char*)filename);
  
  spike_file_t* srccode = spike_file_open(path, O_RDONLY, 0);
  int carrierNum = 0;
  char codebuf;
  int i = 0;
  do //定位到出错行
  {
    spike_file_read(srccode, &codebuf, 1);
    if (codebuf == '\n') {
      carrierNum++;
    }
  } while (carrierNum < linenum - 1);

  do { //输出出错行内容
    spike_file_read(srccode, &codebuf, 1);
    sprint("%c", codebuf);
  } while (codebuf != '\n');

  panic("Illegal instruction!");
}

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      // panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction();
      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
