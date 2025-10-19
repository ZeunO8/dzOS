// proc.c
#include "proc.h"
#include "common/lib.h"
#include "common/power.h"
#include "common/printf.h"
#include "cpu/asm.h"
#include "cpu/fpu.h"
#include "cpu/smp.h"
#include "device/rtc.h"
#include "fs/fs.h"
#include "userspace/exec.h"

/**
 * The kernel stackpointer which we used just before we have switched to
 * userspace.
 */
static uint64_t kernel_stackpointer;

static struct cpu_context kernel_context;

/**
 * Next PID to assign to a program
 */
static uint64_t next_pid = 1;

/**
 * List of processes in the system
 */
static uint64_t process_count = 0;
static uint64_t process_min_index = 0;
static struct process* processes[MAX_PROCESSES];

/**
 * Atomically get the next PID
 */
static inline uint64_t get_next_pid(void) {
  return __atomic_fetch_add(&next_pid, 1, __ATOMIC_RELAXED);
}

extern void context_switch_to_user(struct cpu_context *to_context, struct cpu_context *from_context);

extern void context_switch_to_kernel(struct cpu_context *to_context, struct cpu_context *user_context);

/**
 * Gets the current running process of this CPU core
 */
struct process *my_process(void) { return cpu_local()->running_process; }

/**
 * Unlocks the current running process's lock.
 */
void my_process_unlock(void) { condvar_unlock(&my_process()->lock); };

struct process* is_valid_process_init(size_t i)
{
  static int sz = sizeof(struct process);
  if (processes[i])
    return NULL;
  struct process* proc = (struct process*)kalloc();
  memset(proc, 0, sz);
  processes[i] = proc;
  proc->orig_i = proc->i = i;
  // Make it usable
  proc->state = USED;
  proc->pid = get_next_pid();
  proc->exit_status = -1;
  proc->pagetable = vmm_user_pagetable_new();
  if (proc->pagetable == NULL)
  {
    panic("out of memory");
  }
  proc->current_sbrk = 0;
  proc->initial_data_segment = 0;
  memset(&proc->additional_data, 0, sizeof(proc->additional_data));
  return proc;
}

/**
 * Finds first value in the given process range (start or min end)
 * 
 * If is the start range and finds an existing process, start range is exhausted so skips to end range
 */
#define INIT_PROCESS_RANGE(MIN, MAX, START_RANGE) for (size_t i = MIN; i < MAX; i++) {\
  if ((proc = is_valid_process_init(i)))\
    goto _return;\
  else if (START_RANGE)\
    goto _end_range;\
  else\
    continue;\
}

/**
 * Allocate a new process. Will return NULL on error.
 */
struct process *proc_allocate(void) {
  struct process *proc = NULL;
  // Find a free process slot
  INIT_PROCESS_RANGE(0, process_min_index + 1, 1)
_end_range:
  INIT_PROCESS_RANGE(process_min_index, MAX_PROCESSES, 0)
_return:
  if (proc)
  {
    if (proc->orig_i == process_count)
    {
      process_count++;
    }
    for (size_t i = proc->orig_i + 1; i < MAX_PROCESSES; i++)
    {
      if (!processes[i])
      {
        process_min_index = i;
        goto _end;
      }
    }
    panic("hit process limit - 1");
  }
_end:
  return proc;
}

/**
 * Wakes up one or all processes which are waiting on a waiting channel
 */
void proc_wakeup(void *waiting_channel, bool everyone) {
  struct process *p = my_process();
  bool done = false;
  for (size_t i = 0; i < MAX_PROCESSES; i++) {
    if (!processes[i] || p == processes[i]) // avoid deadlock
      continue;
    condvar_lock(&processes[i]->lock);
    if (processes[i]->state == SLEEPING &&
        processes[i]->waiting_channel == waiting_channel) {
      processes[i]->state = RUNNABLE;
      // If we should wake up one thread only, then we are done
      done = !everyone;
    }
    condvar_unlock(&processes[i]->lock);
    if (done)
      break;
  }
}

/**
 * Allocates a file descriptor of the running process.
 * This function is not thread safe and a process shall not call this
 * function twice in two different threads.
 *
 * Note: Because there is only one thread per process, this function
 * does not need any locks or whatsoever.
 */
int proc_allocate_fd(void) {
  // This is OK to be not locked because each process is
  // currently only single threaded.
  struct process *p = my_process();
  if (p == NULL)
    panic("proc_allocate_fd: no process");
  int fd = -1;
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (p->open_files[i].type == FD_EMPTY) {
      fd = i;
      break;
    }
  }
  return fd; // may be -1
}

/**
 * Exits from the current process ans switches back to the scheduler
 */
void proc_exit(int exit_code) {
  struct process *proc = my_process();

  // Close all files
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (proc->open_files[i].type == FD_INODE) {
      fs_close(proc->open_files[i].structures.inode);
      proc->open_files[i].type = FD_EMPTY; // I don't think I need this
    }
  }

  if (!spinlock_locked(&proc->lock.lock))
    panic("proc should be locked");

  // Set the exit status
  proc->exit_status = exit_code;
  condvar_notify_all(&proc->lock);

  // Set the status to the exited and return back
  proc->state = EXITED;

  scheduler_switch_back();
}

void sys_exit(int ec)
{
  proc_exit(ec);
}

/**
 * Waits until a process is finished and returns its exit value.
 * Will return -1 if the pid does not exist.
 *
 * Note: Because we use locks in scheduler there is no way we can
 * face a race here. However, the is the possibility that the exit
 * code gets lost. On the other hand, we give this process one round
 * in the round robin process in order for another process to wait
 * for it; If no process has waited on this process, the result will
 * be lost.
 */
int proc_wait(uint64_t target_pid) {
  struct process *target_process = NULL;

  // Look for the process with the given pid
  for (size_t i = 0; i < MAX_PROCESSES; i++) {
    if (!processes[i])
      continue;
    condvar_lock(&processes[i]->lock);
    if (processes[i]->pid == target_pid) {
      target_process = processes[i];
      break;
    }
    condvar_unlock(&processes[i]->lock);
  }

  // Did we find the given process?
  if (target_process == NULL)
    return -1;

  // Wait until the status is exited
  while (target_process->state != EXITED)
    condvar_wait(&target_process->lock);

  // Copy the value to a register to prevent races
  int exit_status = target_process->exit_status;
  condvar_unlock(&target_process->lock);
  return exit_status;
}

int sys_wait(uint64_t target_pid)
{
  return proc_wait(target_pid);
}

/**
 * Allocates are deallocates memory by increasing or decreasing the top of the
 * data segment.
 */
void *proc_sbrk(int64_t how_much) {
  struct process *p = my_process();
  void *before = (void *)p->current_sbrk;

  if (how_much > 0) { // allocating memory
    p->current_sbrk =
        vmm_user_sbrk_allocate(p->pagetable, p->current_sbrk, how_much);
  } else if (how_much < 0) { // deallocating memory
    if (p->initial_data_segment <= p->current_sbrk + how_much) {
      // Do not deallocate memory which is not allocated with sbrk
      how_much = p->initial_data_segment - p->current_sbrk;
    }
    p->current_sbrk =
        vmm_user_sbrk_deallocate(p->pagetable, p->current_sbrk, -how_much);
  }
  return before;
}

void* sys_sbrk(int64_t how_much)
{
  return proc_sbrk(how_much);
}

/**
 * Sleep the current process for at least the number of milliseconds given as
 * the argument.
 */
void sys_sleep(uint64_t msec) {
  struct process *proc = my_process();
  const uint64_t target_epoch_wakeup = rtc_now() + msec;
  /**
   * For now, we simply use busy waiting. Just wait until we have
   * reached the desired sleep duration. In each step, we switch back
   * to the scheduler to allow other processes to run.
   */

  condvar_lock(&my_process()->lock); // lock before switch back
  while (target_epoch_wakeup > rtc_now()) {
    proc->state = RUNNABLE;  // we can run this again
    scheduler_switch_back(); // switch back to allow other processes to run
  }
  condvar_unlock(&my_process()->lock);
}

/**
 * Setup the scheduler by creating a process which runs as the very program
 */
void scheduler_init(void) {
  const char *args[] = {"/init", NULL};
  // Run the program
  if (proc_exec("/init", args, NULL) == (uint64_t)-1)
    panic("cannot create /init process");
  if (proc_exec("/init", args, NULL) == (uint64_t)-1)
    panic("cannot create /init process");
  ktprintf("Initialized first userprog\n");
}

/**
 * Call this function from any interrupt or syscall in each user space
 * program in order to switch back to the scheduler and schedule any other
 * program. This is like the very bare bone of the yield function.
 *
 * Before calling this function, the caller should old the my_process()->lock
 */
void scheduler_switch_back(void) {
  struct process *proc = my_process();
  if (!spinlock_locked(&proc->lock.lock))
    panic("scheduler_switch_back: not locked");
  if (proc->state == RUNNING)
    panic("scheduler_switch_back: RUNNING");
  context_switch_to_kernel(&kernel_context, &proc->ctx);
}

static void load_additional_data_if_needed(struct process *old,
                                           const struct process *new) {
  // In this case, we dont need to do anything. Everything is stored
  // in the CPU states.
  if (new == old)
    return;
  // It is important to load the gs base in the kernel gs base
  // in order for it to be swapped with swapgs and be stored in
  // the main gs base.
  wrmsr(MSR_KERNEL_GS_BASE, new->additional_data.gs_base);
  // Save the FPU state of the old process and load the new one
  if (old != NULL) // this might happen at first program
    fpu_save((void *)old->additional_data.fpu_state);
  fpu_load((const void *)new->additional_data.fpu_state);
}

uint64_t process_kstack;

void coelesce_processes(size_t i)
{
  uint64_t prev_count = process_count;
  for (size_t j = i + 1; j < process_count; j++)
  {
    struct process** proc = &processes[j];
    if (proc)
    {
      processes[j - 1] = *proc;
      (*proc)->i = j - 1;
      *proc = 0;
    }
    else
      break;
  }
  if (process_min_index > i)
    process_min_index = i;
  --process_count;
}

/**
 * Scheduler the scheduler of the operating system.
 */
void scheduler(void) {
  ktprintf("Scheduler initiated\n");
  for (;;) {
    if (process_count == 0) {
      system_shutdown();
    }
    for (size_t i = 0; i < process_count; i++) {
      struct process* proc = processes[i];
      if (!proc)
        continue;
      condvar_lock(&proc->lock);
			if (proc->state == RUNNING) {
				proc_check_stack_canary(proc);
			}
      switch (proc->state) {
      case RUNNABLE:
        proc->state = RUNNING; // which are runnable...
        // load program values...
        load_additional_data_if_needed(cpu_local()->last_running_process, proc);
        // and make them running and when found...
        cpu_local()->running_process = proc;
        cpu_local()->last_running_process = proc;
        wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu_local());

        ktprintf("About to step into process %i\n", proc->pid);

        process_kstack = kernel_context.rsp = proc->kernel_stack_top;
        kernel_context.kernel_rip = (uint64_t)&&resume_scheduler;
        // switch to its memory space...
        install_pagetable(V2P(proc->pagetable));
        vmm_flush_tlb();
        // and run it...
        context_switch_to_user(&proc->ctx, &kernel_context);
resume_scheduler:
        ktprintf("switched back from process %i\n", proc->pid);
        cpu_local()->running_process = NULL;
        // until we return and we do everything again!
        break;
      case EXITED:
        ktprintf("process %i EXITED\n", proc->pid);
        // If the pagetable of this process is installed, unload it
        if (get_installed_pagetable() == V2P(proc->pagetable))
          install_pagetable(V2P(kernel_pagetable));
        vmm_free_proc_kernel_stack(proc->orig_i);
        // Free the memory of the process
        vmm_user_pagetable_free(proc->pagetable);
        proc->state = UNUSED;
        proc->pid = 0; // do not give false positive in wait
        proc->current_sbrk = 0;
        proc->initial_data_segment = 0;
        proc->pagetable = NULL;
        memset(&proc->additional_data, 0, sizeof(proc->additional_data));
        // On rare occasions, this might happen
        if (cpu_local()->last_running_process == proc)
          cpu_local()->last_running_process = NULL;
        kfree(proc);
        processes[i] = 0;
        coelesce_processes(i);
        break;
      default:
        break;
      }
      if (spinlock_locked(&proc->lock.lock))
        condvar_unlock(&proc->lock);
    }
  }
}

void proc_init_stack_canary(struct process *proc)
{
	// Use TSC + PID for unique canary
	uint64_t tsc = get_tsc();
	proc->stack_canary = STACK_CANARY_MAGIC ^ tsc ^ proc->pid;
	
	// Write canary at bottom of stack (above guard page)
	uint64_t *canary_location = (uint64_t *)proc->kernel_stack_base;
	*canary_location = proc->stack_canary;
}

void proc_check_stack_canary(struct process *proc)
{
	uint64_t *canary_location = (uint64_t *)proc->kernel_stack_base;
	if (*canary_location != proc->stack_canary) {
		kprintf("\n");
		kprintf("================================================\n");
		kprintf("KERNEL PANIC: Stack Canary Corrupted!\n");
		kprintf("================================================\n");
		kprintf("Process: PID %llu\n", proc->pid);
		kprintf("Expected: 0x%llx\n", proc->stack_canary);
		kprintf("Found:    0x%llx\n", *canary_location);
		kprintf("================================================\n");
		panic("Stack corruption detected");
	}
}