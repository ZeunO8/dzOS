#include "scheduler.h"

#include "cpu/asm.h"
#include "device/pic.h"
#include "device/rtc.h"
#include "mem/kmalloc.h"
#include "common/printf.h"
#include "common/lib.h"
#include "proc.h"
#include "drivers/driver.h"
#include "cpu/idt.h"
#include "cpu/smp.h"
#include "cpu/fpu.h"
#include "common/power.h"
#include "cpu/gdt.h"

// Helper macro for container_of
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

// Global runqueue (single CPU for now)
static runqueue_t g_runqueue;
static sched_stats_t g_stats;

// Timer frequency in microseconds
static uint64_t timer_period_us = 1000000 / SCHED_TIMER_FREQ_HZ;

// ============================================================================
// RUNQUEUE OPERATIONS
// ============================================================================

static void sched_enqueue(runqueue_t *rq, struct process *p) {
    sched_entity_t *se = &p->sched;
    uint8_t prio = se->dynamic_priority;
    
    if (prio >= SCHED_PRIORITY_LEVELS)
        prio = SCHED_PRIORITY_LEVELS - 1;
    
    // Add to tail of priority queue
    se->next = NULL;
    se->prev = rq->queue_tails[prio];
    
    if (rq->queue_tails[prio]) {
        rq->queue_tails[prio]->next = se;
    } else {
        rq->queue_heads[prio] = se;
    }
    rq->queue_tails[prio] = se;
    
    rq->queue_sizes[prio]++;
    rq->total_runnable++;
    rq->total_weight += sched_entity_weight(se);
}

static void sched_dequeue(runqueue_t *rq, struct process *p) {
    sched_entity_t *se = &p->sched;
    uint8_t prio = se->dynamic_priority;
    
    if (prio >= SCHED_PRIORITY_LEVELS)
        return;
    
    // Remove from doubly-linked list
    if (se->prev)
        se->prev->next = se->next;
    else
        rq->queue_heads[prio] = se->next;
    
    if (se->next)
        se->next->prev = se->prev;
    else
        rq->queue_tails[prio] = se->prev;
    
    se->next = se->prev = NULL;
    
    rq->queue_sizes[prio]--;
    rq->total_runnable--;
    rq->total_weight -= sched_entity_weight(se);
}

static struct process *sched_pick_next(runqueue_t *rq) {
    // Find highest priority non-empty queue
    for (int prio = 0; prio < SCHED_PRIORITY_LEVELS; prio++) {
        if (rq->queue_heads[prio]) {
            sched_entity_t *se = rq->queue_heads[prio];
            
            // For RT tasks, simple FIFO
            if (prio <= PRIO_RT_MAX) {
                return container_of(se, struct process, sched);
            }
            
            // For normal tasks, pick by vruntime
            sched_entity_t *best = se;
            uint64_t min_vruntime = se->vruntime;
            
            se = se->next;
            while (se) {
                if (se->vruntime < min_vruntime) {
                    min_vruntime = se->vruntime;
                    best = se;
                }
                se = se->next;
            }
            
            return container_of(best, struct process, sched);
        }
    }
    
    return NULL;
}

// ============================================================================
// PRIORITY AND TIMESLICE CALCULATION
// ============================================================================

static void sched_check_interactive(sched_entity_t *se) {
    // Interactive detection based on sleep/run ratio
    if (se->sleep_time > 0 && se->sum_exec_runtime > 0) {
        uint64_t ratio = (se->sleep_time * 100) / se->sum_exec_runtime;
        
        if (ratio > 150) {  // Sleeps more than 1.5x runtime
            se->flags |= SCHED_FLAG_INTERACTIVE;
        } else if (ratio < 50) {  // Sleeps less than 0.5x runtime
            se->flags |= SCHED_FLAG_CPU_BOUND;
            se->flags &= ~SCHED_FLAG_INTERACTIVE;
        }
    }
}

static void sched_update_priority(sched_entity_t *se) {
    // Don't adjust RT priorities
    if (se->flags & SCHED_FLAG_RT)
        return;
    
    uint8_t base = se->static_priority;
    int adjustment = 0;
    
    // Boost interactive processes
    if (se->flags & SCHED_FLAG_INTERACTIVE) {
        adjustment = -1;  // Higher priority
    }
    
    // Penalize CPU-bound processes
    if (se->flags & SCHED_FLAG_CPU_BOUND) {
        adjustment = 1;   // Lower priority
    }
    
    // Apply nice value
    adjustment += se->nice / 4;  // -5 to +5 adjustment
    
    int new_prio = (int)base + adjustment;
    if (new_prio < PRIO_NORMAL_MIN)
        new_prio = PRIO_NORMAL_MIN;
    if (new_prio > PRIO_IDLE_MAX)
        new_prio = PRIO_IDLE_MAX;
    
    se->dynamic_priority = (uint8_t)new_prio;
}

uint64_t sched_compute_timeslice(sched_entity_t *se) {
    if (se->flags & SCHED_FLAG_RT) {
        return SCHED_MAX_TIMESLICE_US;  // RT gets max timeslice
    }
    
    // Compute based on weight and total system load
    uint64_t weight = sched_entity_weight(se);
    uint64_t total_weight = g_runqueue.total_weight;
    
    if (total_weight == 0)
        return SCHED_MAX_TIMESLICE_US;
    
    // Proportional timeslice
    uint64_t timeslice = (SCHED_MAX_TIMESLICE_US * weight) / total_weight;
    
    // Clamp to min/max
    if (timeslice < SCHED_MIN_TIMESLICE_US)
        timeslice = SCHED_MIN_TIMESLICE_US;
    if (timeslice > SCHED_MAX_TIMESLICE_US)
        timeslice = SCHED_MAX_TIMESLICE_US;
    
    // Interactive boost
    if (se->flags & SCHED_FLAG_INTERACTIVE)
        timeslice = (timeslice * 3) / 2;  // 1.5x boost
    
    return timeslice;
}

// ============================================================================
// VRUNTIME MANAGEMENT (CFS-inspired)
// ============================================================================

static void sched_update_vruntime(sched_entity_t *se, uint64_t delta_us) {
    // Convert physical time to virtual time based on weight
    uint64_t weight = sched_entity_weight(se);
    uint64_t vdelta = (delta_us * 1024) / weight;  // Scaled by weight
    
    se->vruntime += vdelta;
    se->sum_exec_runtime += delta_us;
    
    // Update min vruntime
    if (se->vruntime < g_runqueue.min_vruntime)
        g_runqueue.min_vruntime = se->vruntime;
}

// ============================================================================
// SCHEDULER CORE FUNCTIONS
// ============================================================================

void sched_fork(struct process *p) {
    sched_entity_t *se = &p->sched;
    
    memset(se, 0, sizeof(sched_entity_t));
    
    // Inherit parent priority or set default
    se->static_priority = PRIO_NORMAL_MIN;
    se->dynamic_priority = PRIO_NORMAL_MIN;
    se->nice = 0;
    
    // Start with current min vruntime (fair start)
    se->vruntime = g_runqueue.min_vruntime;
    
    se->last_timeslice = sched_compute_timeslice(se);
}

void sched_sleep(struct process *p, void *wchan) {
    spinlock_lock(&g_runqueue.lock);
    
    sched_entity_t *se = &p->sched;
    uint64_t now = rtc_now();
    
    // Track sleep time for interactivity detection
    if (se->last_ran > 0) {
        uint64_t sleep_duration = now - se->last_ran;
        se->sleep_time += sleep_duration;
        
        // Update rolling average
        se->sleep_avg = (se->sleep_avg * 7 + sleep_duration) / 8;
    }
    
    sched_dequeue(&g_runqueue, p);
    p->state = SLEEPING;
    p->waiting_channel = wchan;
    
    sched_check_interactive(se);
    
    spinlock_unlock(&g_runqueue.lock);
}

void sched_wakeup(struct process *p) {
    spinlock_lock(&g_runqueue.lock);
    
    if (p->state != SLEEPING) {
        spinlock_unlock(&g_runqueue.lock);
        return;
    }
    
    p->state = RUNNABLE;
    p->waiting_channel = NULL;
    
    // Re-check priority on wakeup
    sched_update_priority(&p->sched);
    
    sched_enqueue(&g_runqueue, p);
    
    spinlock_unlock(&g_runqueue.lock);
}

void scheduler_tick(interrupt_frame_t* frame) {
    g_stats.total_timer_ticks++;
    
    struct process *curr = g_runqueue.curr;
    if (!curr || curr->state != RUNNING)
        return;
    
    sched_entity_t *se = &curr->sched;
    uint64_t now = rtc_now();
    
    // Update runtime
    if (se->exec_start > 0) {
        uint64_t delta = now - se->exec_start;
        sched_update_vruntime(se, delta);
        se->exec_start = now;
    }
    
    // Check if timeslice expired
    uint64_t runtime_this_slice = now - se->last_ran;
    if (runtime_this_slice >= se->last_timeslice) {
        // Time to preempt
        g_stats.total_preemptions++;
        scheduler_preempt(frame);
    }
}

void scheduler_preempt(interrupt_frame_t* frame) {
    struct process *curr = g_runqueue.curr;
    if (!curr)
        return;
    
    // condvar_lock(&curr->lock);
    
    if (curr->state == RUNNING) {
        curr->state = RUNNABLE;
    }
    
    // Will trigger reschedule
    scheduler_switch_back(frame);
}

void scheduler_yield(interrupt_frame_t* frame) {
    g_stats.total_yields++;
    
    struct process *curr = my_process();
    if (!curr)
        return;
    
    condvar_lock(&curr->lock);
    curr->state = RUNNABLE;
    scheduler_switch_back(0);
}

// ============================================================================
// TIMER INTERRUPT SETUP
// ============================================================================

extern void isr_timer_stub(void);  // Defined in isr_stubs.S

void sched_timer_handler(interrupt_frame_t* frame) {
    // Update runqueue clock
    g_runqueue.clock = rtc_now();
    
    // Send EOI
    lapic_send_eoi();
    
    // Call scheduler tick
    scheduler_tick(frame);
}

extern device_t* g_rtc_dev;

void sched_timer_init(void) {
    ktprintf("[SCHED] Initializing LAPIC timer for preemption\n");
    
    // Use RTC to get TSC frequency for calibration
    uint64_t tsc_freq;
    driver_ioctl(g_rtc_dev, 1, (uintptr_t)&tsc_freq);
    
    // Calculate timer divisor for desired frequency
    uint64_t ticks_per_interrupt = tsc_freq / SCHED_TIMER_FREQ_HZ;
    
    // Configure LAPIC timer
    // Disable timer during setup
    lapic_write(LAPIC_TIMER, LAPIC_MASKED);
    
    // Set divide configuration (divide by 1)
    lapic_write(LAPIC_TDCR, LAPIC_X1_DIV);
    
    // Set initial count
    lapic_write(LAPIC_TICR, (uint32_t)ticks_per_interrupt);
    
    // Set timer to periodic mode with vector
    uint8_t timer_vector = T_IRQ0 + 0;  // IRQ0 for timer
    lapic_write(LAPIC_TIMER, timer_vector | LAPIC_PERIODIC);
    
    // Register interrupt handler
    idt_set_gate(timer_vector, (uint64_t)isr_timer_stub, 0, 0x8E);
    
    ktprintf("[SCHED] Timer initialized: %lu Hz (%lu ticks)\n", 
             (unsigned long)SCHED_TIMER_FREQ_HZ, 
             (unsigned long)ticks_per_interrupt);
}

// ============================================================================
// MAIN SCHEDULER LOOP
// ============================================================================

extern struct cpu_context kernel_context;

extern uint64_t process_count;
extern uint64_t process_min_index;
extern struct process* processes[MAX_PROCESSES];

extern void context_switch_to_user(struct cpu_context *to_context, struct cpu_context *from_context);
extern void context_switch_to_kernel(struct cpu_context *to_context, struct cpu_context *user_context, interrupt_frame_t* frame);


void load_additional_data_if_needed(struct process *old, const struct process *new) {
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

void scheduler_start(void) {
    ktprintf("[SCHED] Starting preemptive scheduler\n");
    
    // Initialize runqueue
    g_runqueue.cpu_id = 0;
    
    // Initialize statistics
    memset(&g_stats, 0, sizeof(g_stats));
    
    // Initialize timer
    sched_timer_init();
    
    // Main scheduling loop
    for (;;) {
        spinlock_lock(&g_runqueue.lock);
        
        // Check if we have any runnable processes
        if (g_runqueue.total_runnable == 0) {
            spinlock_unlock(&g_runqueue.lock);
            
            // No processes to run - idle
            g_stats.idle_time++;
            
            // Check if all processes exited
            bool all_done = true;
            for (size_t i = 0; i < process_count; i++) {
                if (processes[i] && processes[i]->state != UNUSED) {
                    all_done = false;
                    break;
                }
            }
            
            if (all_done) {
                system_shutdown();
            }
            
            // Wait for interrupt
            ktprintf("Reached %i processes and not all complete, halting.", 0);
            sti();
            __asm__ volatile("hlt");
            cli();
            continue;
        }
        
        // Pick next process to run
        struct process *next = sched_pick_next(&g_runqueue);
        
        if (!next) {
            spinlock_unlock(&g_runqueue.lock);
            continue;
        }
        
        // Remove from runqueue while running
        sched_dequeue(&g_runqueue, next);
        
        // Set as current
        g_runqueue.curr = next;
        next->state = RUNNING;
        
        spinlock_unlock(&g_runqueue.lock);
        
        // Update scheduling entity
        sched_entity_t *se = &next->sched;
        se->exec_start = rtc_now();
        se->last_ran = se->exec_start;
        se->last_timeslice = sched_compute_timeslice(se);
        
        // Load additional process data
        load_additional_data_if_needed(cpu_local()->last_running_process, next);
        cpu_local()->running_process = next;
        cpu_local()->last_running_process = next;
        
        // Update GS base for current CPU
        wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu_local());
        
        // ktprintf("[SCHED] Running PID %llu (prio=%u, timeslice=%lluus)\n",
        //          next->pid, se->dynamic_priority, se->last_timeslice);
        
        // Prepare kernel context
        kernel_context.rsp = next->kernel_stack_top;
        kernel_context.kernel_rip = (uint64_t)&&resume_scheduler;
        
        // Switch to process address space
        install_pagetable(V2P(next->pagetable));
        vmm_flush_tlb();
        
        // Context switch to user
        g_stats.total_switches++;

        condvar_lock(&next->lock);
        context_switch_to_user(&next->ctx, &kernel_context);
        
resume_scheduler:
        condvar_unlock(&next->lock);

        // We're back from the process
        // ktprintf("[SCHED] Returned from PID %llu\n", next->pid);
        
        spinlock_lock(&g_runqueue.lock);
        
        // Update vruntime for the time slice used
        uint64_t now = rtc_now();
        uint64_t delta = now - se->exec_start;
        sched_update_vruntime(se, delta);
        
        // Check and update priority
        sched_check_interactive(se);
        sched_update_priority(se);
        
        // Handle process state
        switch (next->state) {
        case RUNNABLE:
            // Re-enqueue for next time
            sched_enqueue(&g_runqueue, next);
            break;
            
        case SLEEPING:
            // Already dequeued, waiting for wakeup
            break;
            
        case EXITED:
            // Clean up
            ktprintf("[SCHED] Process %llu exited\n", next->pid);
            
            if (get_installed_pagetable() == V2P(next->pagetable))
                install_pagetable(V2P(kernel_pagetable));
            
            vmm_free_proc_kernel_stack(next->orig_i);
            vmm_user_pagetable_free(next->pagetable);
            
            next->state = UNUSED;
            next->pid = 0;
            
            if (cpu_local()->last_running_process == next)
                cpu_local()->last_running_process = NULL;
            
            kfree(next);
            processes[next->i] = NULL;
            coelesce_processes(next->i);
            break;
            
        default:
            break;
        }
        
        g_runqueue.curr = NULL;
        cpu_local()->running_process = NULL;
        
        spinlock_unlock(&g_runqueue.lock);
    }
}

// ============================================================================
// PRIORITY MANAGEMENT API
// ============================================================================

void sched_set_priority(struct process *p, uint8_t prio) {
    if (prio >= SCHED_PRIORITY_LEVELS)
        return;
    
    spinlock_lock(&g_runqueue.lock);
    
    sched_entity_t *se = &p->sched;
    
    // Real-time flag
    if (prio <= PRIO_RT_MAX) {
        se->flags |= SCHED_FLAG_RT;
    } else {
        se->flags &= ~SCHED_FLAG_RT;
    }
    
    se->static_priority = prio;
    se->dynamic_priority = prio;
    
    // Re-enqueue if runnable to update position
    if (p->state == RUNNABLE) {
        sched_dequeue(&g_runqueue, p);
        sched_enqueue(&g_runqueue, p);
    }
    
    spinlock_unlock(&g_runqueue.lock);
}

void sched_nice(struct process *p, int8_t nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    
    spinlock_lock(&g_runqueue.lock);
    
    p->sched.nice = nice;
    sched_update_priority(&p->sched);
    
    spinlock_unlock(&g_runqueue.lock);
}

// ============================================================================
// STATISTICS
// ============================================================================

sched_stats_t sched_get_stats(void) {
    return g_stats;
}

void sched_print_stats(void) {
    ktprintf("\n=== Scheduler Statistics ===\n");
    ktprintf("Total switches:     %llu\n", g_stats.total_switches);
    ktprintf("Total preemptions:  %llu\n", g_stats.total_preemptions);
    ktprintf("Total yields:       %llu\n", g_stats.total_yields);
    ktprintf("Total timer ticks:  %llu\n", g_stats.total_timer_ticks);
    ktprintf("Idle time:          %llu\n", g_stats.idle_time);
    ktprintf("Runnable processes: %u\n", g_runqueue.total_runnable);
    ktprintf("Min vruntime:       %llu\n", g_runqueue.min_vruntime);
    ktprintf("============================\n\n");
}
