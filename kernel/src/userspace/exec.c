// exec.c
#include "exec.h"
#include "common/lib.h"
#include "common/printf.h"
#include "device/serial_port.h"
#include "fs/dzfs.h"
#include "fs/device.h"
#include "fs/fs.h"
#include <zos/exec.h>
#include <zos/file.h>
#include "mem/mem.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "userspace/proc.h"

char *validate_user_string(const char *user_str, size_t max_len);
bool validate_user_read(const void *ptr, size_t len);
bool validate_user_write(void *ptr, size_t len);

// "\x7FELF" in little endian
#define ELF_MAGIC 0x464C457FU

// File header
struct __attribute__((packed)) ElfHeader
{
    uint32_t magic;
    uint8_t elf[12];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};
_Static_assert(sizeof(struct ElfHeader) == 64, "ElfHeader must be 64");

struct __attribute__((packed)) ProgramHeader
{
    uint32_t type;
    uint32_t flags;
    uint64_t off;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};
_Static_assert(sizeof(struct ProgramHeader) == 56, "ProgramHeader must be 56 bytes");

// Values for Proghdr type
#define ELF_PROG_LOAD 1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC 1
#define ELF_PROG_FLAG_WRITE 2
#define ELF_PROG_FLAG_READ 4

static pte_permissions flags2perm(int flags)
{
    // Always userspace
    pte_permissions perm = {.executable = 0, .userspace = 1, .writable = 0};
    if (flags & 0x1)
        perm.executable = 1;
    if (flags & 0x2)
        perm.writable = 1;
    return perm;
}

/**
 * Loads a segment from the ELF file to the memory. The pages of the ELF file
 * must be already allocated.
 */
static int load_segment(pagetable_t pagetable, struct fs_inode *ip, uint64_t va,
                        uint32_t offset, uint32_t sz)
{
    /**
     * Note: You might wonder: Why we get the physical address instead of just
     * using the virtual address if the page table is mapped? Well, in some cases
     * (like .rodata or .text) the MMU maps that section as read only. Thus, we
     * cannot write to that data. Instead, we can write to the physical address of
     * the frame and that works just fine.
     */
    for (uint32_t i = 0; i < sz; i += PAGE_SIZE)
    {
        uint64_t physical_address = vmm_walkaddr(pagetable, va + i, true);
        if (physical_address == 0)
            panic("load_segment: address should exist");
        uint64_t n;
        if (sz - i < PAGE_SIZE)
            n = sz - i;
        else
            n = PAGE_SIZE;
        if (fs_read(ip, (char *)P2V(physical_address), n, offset + i) != (int)n)
            return -1;
    }
    return 0;
}

#define USER_DEFAULT_LOAD_BASE 0x00400000ULL

// Executable file
#ifndef ELF_ET_EXEC
#define ELF_ET_EXEC 2
#endif

// Shared object file (Position Independent Executable)
#ifndef ELF_ET_DYN
#define ELF_ET_DYN 3
#endif

uint64_t sys_exec(const char* path, const char* args[])
{
	// Validate path
	char *kernel_path = validate_user_string(path, MAX_PATH_LENGTH);
	if (!kernel_path) {
		return -1;
	}

	// Validate args array pointer
	if (args && !validate_user_read(args, sizeof(char *) * MAX_ARGV)) {
		kmfree(kernel_path);
		return -1;
	}

	// Copy args to kernel space
	char *kernel_args[MAX_ARGV] = {NULL};
	if (args) {
		for (int i = 0; i < MAX_ARGV && args[i]; i++) {
			kernel_args[i] = validate_user_string(args[i], MAX_PATH_LENGTH);
			if (!kernel_args[i]) {
				// Cleanup already copied args
				for (int j = 0; j < i; j++) {
					kmfree(kernel_args[j]);
				}
				kmfree(kernel_path);
				return -1;
			}
		}
	}

	// Execute with validated arguments
	const char *kernel_args_const[MAX_ARGV];
	for (int i = 0; i < MAX_ARGV; i++) {
		kernel_args_const[i] = kernel_args[i];
	}

	uint64_t result = proc_exec(kernel_path, kernel_args_const, my_process()->working_directory);

	// Cleanup
	kmfree(kernel_path);
	for (int i = 0; i < MAX_ARGV && kernel_args[i]; i++) {
		kmfree(kernel_args[i]);
	}

	return result;
}

/**
 * Creates a new process as the child of the running process.
 * Working directory is the working directory which the parent process
 * is in.
 *
 * Returns the new PID as the result if successful. Otherwise returns
 * -1 which is an error.
 */
uint64_t proc_exec(const char *path, const char *args[], struct fs_inode *working_directory)
{
    struct ElfHeader elf;
    struct ProgramHeader ph;
    struct process *proc = NULL;
    struct fs_inode *proc_inode = NULL;
    uint64_t load_bias = 0;

    // Open the executable
    proc_inode = fs_open(path, working_directory, 0);
    if (!proc_inode) goto bad;
    
    if (fs_read(proc_inode, (char *)&elf, sizeof(elf), 0) != sizeof(elf)) goto bad;
    if (elf.magic != ELF_MAGIC) goto bad;

    // Allocate process and user pagetable
    proc = proc_allocate();
    if (!proc) goto bad;

    // Determine load bias
    if (elf.type == ELF_ET_DYN) load_bias = USER_DEFAULT_LOAD_BASE;
    else if (elf.type == ELF_ET_EXEC)
        load_bias = (elf.entry == 0 || elf.entry < USERSPACE_VA_MIN || elf.entry >= USERSPACE_VA_MAX) ? USER_DEFAULT_LOAD_BASE : 0;
    else { ktprintf("exec: unsupported ELF type %u\n", elf.type); goto bad; }

    // Load program segments
    for (uint64_t i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if (fs_read(proc_inode, (char *)&ph, sizeof(ph), off) != sizeof(ph)) {
            goto bad;
        }

        if (ph.type != ELF_PROG_LOAD) {
            continue;
        }
        if (ph.memsz < ph.filesz) {
            goto bad;
        }

        uint64_t mapped_va = ph.vaddr + load_bias;
        uint64_t map_start = mapped_va & ~(PAGE_SIZE - 1);
        uint64_t map_offset = mapped_va - map_start;
        uint64_t alloc_size = PAGE_ROUND_UP(map_offset + ph.memsz);

        if (map_start < USERSPACE_VA_MIN || (map_start + alloc_size) > USERSPACE_VA_MAX) {
            goto bad;
        }

        if (vmm_allocate(proc->pagetable, map_start, alloc_size, flags2perm(ph.flags), false) == -1) {
            goto bad;
        }

        if (ph.filesz > 0) {
            if (load_segment(proc->pagetable, proc_inode, map_start + map_offset, ph.off, ph.filesz) < 0) {
                goto bad;
            }
        }
        
        if (ph.memsz > ph.filesz) {
            if (vmm_zero(proc->pagetable, map_start + map_offset + ph.filesz, ph.memsz - ph.filesz) < 0) {
                goto bad;
            }
        }

        proc->initial_data_segment = MAX_SAFE(proc->initial_data_segment, map_start + alloc_size);
    }

    const char *envp[] = {0};

    uint64_t sp = USER_STACK_TOP;
    uint64_t argv_ptrs[MAX_ARGV] = {0};
    uint64_t envp_ptrs[MAX_ENVP] = {0};
    int argc = 0, envc = 0;

    // Copy argument strings to stack
    if (args) {
        for (; argc < MAX_ARGV && args[argc]; argc++) {
            size_t len = strlen(args[argc]);
            sp -= len + 1;
            vmm_memcpy(proc->pagetable, sp, args[argc], len + 1, true);
            argv_ptrs[argc] = sp;
        }
    }

    // Copy environment strings (optional, can be none)
    // if (envp) {
    for (; envc < MAX_ENVP && envp[envc]; envc++) {
        size_t len = strlen(envp[envc]);
        sp -= len + 1;
        vmm_memcpy(proc->pagetable, sp, envp[envc], len + 1, true);
        envp_ptrs[envc] = sp;
    }
    // }

    // Push envp array
    sp -= 8;
    uint64_t zero = 0;
    vmm_memcpy(proc->pagetable, sp, &zero, 8, true); // envp NULL
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;
        vmm_memcpy(proc->pagetable, sp, &envp_ptrs[i], 8, true);
    }
    uint64_t envp_base = sp;

    // Push argv array
    sp -= 8;
    vmm_memcpy(proc->pagetable, sp, &zero, 8, true); // argv NULL
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        vmm_memcpy(proc->pagetable, sp, &argv_ptrs[i], 8, true);
    }
    uint64_t argv_base = sp;

    // Push argc
    sp -= 8;
    uint64_t argc64 = argc;
    vmm_memcpy(proc->pagetable, sp, &argc64, 8, true);

    // Align to 16 bytes (SysV ABI)
    sp &= ~0xF;

    // Fill context for sysretq transition
    uint64_t entry_virtual = load_bias + elf.entry;

    // Open stdin, stdout, stderr
    int serial_idx = device_index(SERIAL_DEVICE_NAME);
    if (serial_idx == -1) panic("exec: no serial");

    proc->open_files[DEFAULT_STDIN].type = FD_DEVICE;
    proc->open_files[DEFAULT_STDIN].structures.device = serial_idx;
    proc->open_files[DEFAULT_STDIN].offset = 0;
    proc->open_files[DEFAULT_STDIN].readble = true;
    proc->open_files[DEFAULT_STDIN].writable = false;

    proc->open_files[DEFAULT_STDOUT].type = FD_DEVICE;
    proc->open_files[DEFAULT_STDOUT].structures.device = serial_idx;
    proc->open_files[DEFAULT_STDOUT].offset = 0;
    proc->open_files[DEFAULT_STDOUT].readble = false;
    proc->open_files[DEFAULT_STDOUT].writable = true;

    proc->open_files[DEFAULT_STDERR].type = FD_DEVICE;
    proc->open_files[DEFAULT_STDERR].structures.device = serial_idx;
    proc->open_files[DEFAULT_STDERR].offset = 0;
    proc->open_files[DEFAULT_STDERR].readble = false;
    proc->open_files[DEFAULT_STDERR].writable = true;

    // Initialize heap pointers
    proc->initial_data_segment = PAGE_ROUND_UP(proc->initial_data_segment);
    proc->current_sbrk = proc->initial_data_segment;

    proc->kernel_stack_top = vmm_allocate_proc_kernel_stack(proc->i);
    proc->kernel_stack_base = proc->kernel_stack_top - KERNEL_STACK_SIZE;

    memset(&proc->ctx, 0, sizeof(proc->ctx));

    // Setup CPU context for first entry to user mode
    proc->ctx.rip = entry_virtual; // User entry point
    proc->ctx.rsp = sp; // User stack top
    proc->ctx.rflags = 0x202; // Interrupt enable flag

    proc_init_stack_canary(proc);

    // Handle working directory
    fs_close(proc_inode);
    if (!working_directory) working_directory = fs_open("/", NULL, DZFS_O_DIR);
    else fs_dup(working_directory);
    if (!working_directory) panic("exec: NULL working directory");
    proc->working_directory = working_directory;

    // Mark process runnable
    proc->state = RUNNABLE;
    return proc->pid;

bad:
    if (proc_inode) fs_close(proc_inode);
    if (proc) { vmm_user_pagetable_free(proc->pagetable); proc->state = UNUSED; proc->pid = 0; }
    return -1;
}