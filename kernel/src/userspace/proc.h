// proc.h
#pragma once
#include "common/condvar.h"
#include "fs/file.h"
#include "mem/vmm.h"
#include <stddef.h>
#include <stdint.h>

#define STACK_CANARY_MAGIC 0xDEADBEEFCAFEBABEULL

/**
 * Each process that this process can be in
 */
enum process_state { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, EXITED };

/**
 * When we switch out or switch in the process, we shall save/load
 * the context of the program. This context is stored here. This is basically
 * a part of the stack of the program because we push our context values on
 * the stack.
 */
struct cpu_context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    // The order above is callee-saved first to match many conventions,
    // but we save/restore all GPRs below for safety.

    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;

    uint64_t rsp;    // user stack pointer to be loaded before sysret
    uint64_t rip;    // user instruction pointer -> loaded into rcx for sysretq
    uint64_t rflags; // -> loaded into r11 for sysretq

    uint64_t kernel_rip;
};

/**
 * Maximum number of files which a process can open
 */
#define MAX_OPEN_FILES 8

/**
 * Maximum number of processes (TODO: set to 64bit range)
 */
#define MAX_PROCESSES 64

/**
 * Process specific metadata which we restore just before switching to this
 * process
 */
struct process_data {
  // The GS segment base which we store in MSR
  uint64_t gs_base;
  // FPU state loaded and saved with fxsave/fxrstor
  __attribute__((aligned(16))) uint8_t fpu_state[512];
};

/**
 * Each process can be represented with this
 */
struct process {
  /**
  * Kernel stack layout per process (20KB total):
  * 
  *   High Address (grows down)
  *   +------------------+ <- kernel_stack_top (RSP starts here)
  *   |                  |
  *   |  Actual Stack    | 16KB (KERNEL_STACK_SIZE)
  *   |  (grows down)    |
  *   |                  |
  *   +------------------+ <- kernel_stack_base
  *   |                  |
  *   |  Guard Page      | 4KB (unmapped, causes page fault)
  *   |  (NOT PRESENT)   |
  *   |                  |
  *   +------------------+
  *   Low Address
  * 
  * Stack overflow will hit guard page → page fault → panic
  */
  uint64_t kernel_stack_top;   // top of kernel stack (highest address)
  uint64_t kernel_stack_base;  // start of kernel stack (lowest address)

  // The process ID
  uint64_t pid;

  // The (original) process index
  uint64_t orig_i;

  // The (current) process index
  uint64_t i;
  // If we switch our stack pointer to this, we will resume the program
  // uint64_t resume_stack_pointer;
  // The pagetable of this process
  pagetable_t pagetable;
  // Files open for this process. The index is the fd in the process.
  struct process_file open_files[MAX_OPEN_FILES];
  // Top of the initial data segment.
  uint64_t initial_data_segment;
  // The value returned by sbrk(0)
  uint64_t current_sbrk;
  // Current working directory inode
  struct fs_inode *working_directory;

  struct cpu_context ctx;
  // Store some more specific process data here.
  // We avoid saving/loading these data if the the next process which
  // is going to be scheduled is the same as the old process.
  struct process_data additional_data;
  // The condvar which guards all variables below.
  // Programs might wait on this lock if they are using the wait
  // system call.
  struct condvar lock;
  // On what object are we waiting on if sleeping?
  void *waiting_channel;
  // The exit status of this application
  int exit_status;
  // What is going on in this process?
  enum process_state state;

  uint64_t stack_canary;
};

struct process *my_process(void);
struct process *proc_allocate(void);
void proc_wakeup(void *waiting_channel, bool everyone);
int proc_allocate_fd(void);
void proc_exit(int exit_code);
int proc_wait(uint64_t pid);
void *proc_sbrk(int64_t how_much);
void sys_sleep(uint64_t msec);
void scheduler_init(void);
void scheduler_switch_back(void);
void scheduler(void);

void proc_init_stack_canary(struct process *proc);
void proc_check_stack_canary(struct process *proc);