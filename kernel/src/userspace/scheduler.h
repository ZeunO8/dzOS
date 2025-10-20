#pragma once
#include "common/spinlock.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// SCHEDULER CONFIGURATION
// ============================================================================

#define SCHED_PRIORITY_LEVELS 8      // Number of priority queues
#define SCHED_MIN_TIMESLICE_US 1000  // 1ms minimum timeslice
#define SCHED_MAX_TIMESLICE_US 10000 // 10ms maximum timeslice
#define SCHED_TIMER_FREQ_HZ 1000     // 1ms timer tick
#define SCHED_INTERACTIVE_THRESHOLD 5000 // 5ms interactive detection

// Priority ranges
#define PRIO_RT_MIN 0                // Real-time min priority
#define PRIO_RT_MAX 2                // Real-time max priority
#define PRIO_NORMAL_MIN 3            // Normal min priority
#define PRIO_NORMAL_MAX 5            // Normal max priority
#define PRIO_IDLE_MIN 6              // Idle min priority
#define PRIO_IDLE_MAX 7              // Idle max priority

// ============================================================================
// PROCESS SCHEDULING METADATA
// ============================================================================

typedef struct sched_entity {
    // Timing information
    uint64_t vruntime;           // Virtual runtime (for fairness)
    uint64_t exec_start;         // When process started executing
    uint64_t sum_exec_runtime;   // Total CPU time used
    uint64_t last_timeslice;     // Last assigned timeslice
    
    // Priority information
    uint8_t static_priority;     // Base priority (0-7)
    uint8_t dynamic_priority;    // Current effective priority
    int8_t nice;                 // Nice value (-20 to +19)
    
    // Scheduling policy flags
    uint32_t flags;
    #define SCHED_FLAG_RT        (1 << 0)  // Real-time process
    #define SCHED_FLAG_INTERACTIVE (1 << 1) // Interactive process
    #define SCHED_FLAG_CPU_BOUND (1 << 2)  // CPU-bound process
    
    // Interactivity detection
    uint64_t sleep_time;         // Time spent sleeping
    uint64_t last_ran;           // Last time process ran
    uint32_t sleep_avg;          // Rolling average of sleep time
    
    // Queue management
    struct sched_entity *next;   // Next in priority queue
    struct sched_entity *prev;   // Previous in priority queue
} sched_entity_t;

// ============================================================================
// RUNQUEUE STRUCTURE
// ============================================================================

struct process;

typedef struct runqueue {
    // Priority queues (one per priority level)
    sched_entity_t *queue_heads[SCHED_PRIORITY_LEVELS];
    sched_entity_t *queue_tails[SCHED_PRIORITY_LEVELS];
    
    // Queue statistics
    uint32_t queue_sizes[SCHED_PRIORITY_LEVELS];
    uint32_t total_runnable;
    
    // Timing
    uint64_t clock;              // Runqueue clock
    uint64_t prev_clock;         // Previous clock value
    
    // Current running process
    struct process *curr;
    
    // Load tracking
    uint64_t total_weight;       // Sum of all process weights
    uint64_t min_vruntime;       // Minimum vruntime in queue
    
    // CPU affinity (for future SMP)
    uint8_t cpu_id;
    
    struct spinlock lock;
} runqueue_t;

// ============================================================================
// SCHEDULER STATISTICS
// ============================================================================

typedef struct sched_stats {
    uint64_t total_switches;
    uint64_t total_preemptions;
    uint64_t total_yields;
    uint64_t total_timer_ticks;
    uint64_t idle_time;
} sched_stats_t;

// ============================================================================
// SCHEDULER API
// ============================================================================

void scheduler_start(void);
void scheduler_tick(void);           // Called by timer interrupt
void scheduler_yield(void);          // Voluntary yield
void scheduler_preempt(void);        // Forced preemption

// Process management
void sched_fork(struct process *p);
void sched_exit(struct process *p);
void sched_sleep(struct process *p, void *wchan);
void sched_wakeup(struct process *p);

// Priority management
void sched_set_priority(struct process *p, uint8_t prio);
void sched_nice(struct process *p, int8_t nice);
uint64_t sched_compute_timeslice(sched_entity_t *se);

// Statistics
sched_stats_t sched_get_stats(void);
void sched_print_stats(void);

// ============================================================================
// INTERNAL SCHEDULER FUNCTIONS
// ============================================================================

static inline uint64_t sched_entity_weight(sched_entity_t *se) {
    // Weight calculation: higher priority = more weight
    // Uses exponential scaling for fairness
    static const uint32_t weights[8] = {
        88761,  // Priority 0 (RT high)
        71755,  // Priority 1 (RT)
        56483,  // Priority 2 (RT low)
        46273,  // Priority 3 (Normal high)
        36291,  // Priority 4 (Normal)
        29154,  // Priority 5 (Normal low)
        23254,  // Priority 6 (Idle high)
        15000   // Priority 7 (Idle)
    };
    return weights[se->dynamic_priority];
}

static inline bool sched_is_rt(sched_entity_t *se) {
    return se->dynamic_priority <= PRIO_RT_MAX;
}

static inline bool sched_is_interactive(sched_entity_t *se) {
    return (se->flags & SCHED_FLAG_INTERACTIVE) != 0;
}

// Enqueue/dequeue operations
static void sched_enqueue(runqueue_t *rq, struct process *p);
static void sched_dequeue(runqueue_t *rq, struct process *p);
static struct process *sched_pick_next(runqueue_t *rq);

// Priority adjustment
static void sched_update_priority(sched_entity_t *se);
static void sched_check_interactive(sched_entity_t *se);

// Timer management
void sched_timer_init(void);
void sched_timer_handler(void);