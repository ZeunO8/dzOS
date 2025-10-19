// drv_nvme.c NVMe Driver - Block device driver for NVMe storage
#include "driver.h"
#include "hw_detect.h"
#include "common/lib.h"
#include "common/printf.h"
#include "mem/mem.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include <stddef.h>

// NVMe register offsets and constants
#define NVME_CAP_OFFSET 0x0000
#define NVME_CC_OFFSET 0x0014
#define NVME_CSTS_OFFSET 0x001c
#define NVME_AQA_OFFSET 0x0024
#define NVME_ASQ_OFFSET 0x0028
#define NVME_ACQ_OFFSET 0x0030

#define NVME_ADMIN_QUEUE_SIZE 2
#define NVME_IO_QUEUE_SIZE 16
#define NVME_PAGE_SIZE_BITS 12
#define NVME_PAGE_SIZE (1ULL << NVME_PAGE_SIZE_BITS)
#define NVME_NAMESPACE_INDEX 1

#define NVME_CAP_DSTRD(x) (1 << (2 + (((x) >> 32) & 0xf)))
#define NVME_SQTDBL_OFFSET(QID, DSTRD) (0x1000 + ((2 * (QID)) * (DSTRD)))
#define NVME_CQHDBL_OFFSET(QID, DSTRD) (0x1000 + (((2 * (QID)) + 1) * (DSTRD)))

#define NVME_REG4(base, offset) (*((uint32_t volatile *)((base) + (offset))))
#define NVME_REG8(base, offset) (*((uint64_t volatile *)((base) + (offset))))

// NVMe command structures
typedef struct {
    uint8_t opc;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t rsvd1;
    uint64_t mptr;
    uint64_t prp[2];
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sq_entry_t;

typedef struct {
    uint32_t cdw0;
    uint32_t rsvd1;
    uint16_t sqhd;
    uint16_t sqid;
    uint16_t cid;
    uint16_t flags;
} nvme_cq_entry_t;

typedef struct {
    uint16_t ms;
    uint8_t lbads;
    uint8_t rp;
} nvme_lbaformat_t;

typedef struct {
    uint64_t nsze;
    uint64_t ncap;
    uint64_t nuse;
    uint8_t nsfeat;
    uint8_t nlbaf;
    uint8_t flbas;
    uint8_t mc;
    uint8_t dpc;
    uint8_t dps;
    uint8_t nmic;
    uint8_t rescap;
    uint8_t rsvd1[88];
    uint64_t eui64;
    nvme_lbaformat_t lba_format[16];
    uint8_t rsvd2[192];
    uint8_t vendor_data[3712];
} nvme_namespace_data_t;

typedef struct {
    volatile nvme_sq_entry_t *submission_queue;
    volatile nvme_cq_entry_t *completion_queue;
    uint32_t submission_queue_tail;
    uint32_t completion_queue_head;
    uint32_t queue_index;
    uint32_t queue_size;
    uint8_t completion_queue_current_phase;
} nvme_queue_t;

typedef struct {
    void *base;
    uint64_t cap;
    nvme_queue_t admin_queue;
    nvme_queue_t io_queue;
    uint64_t total_blocks;
    uint32_t block_size;
    uint16_t next_command_id;
} nvme_device_data_t;

// Global device counter for unique naming
static uint32_t g_nvme_device_count = 0;

#define NEXT_CID(nvme) (__atomic_fetch_add(&(nvme)->next_command_id, 1, __ATOMIC_RELAXED))

static void nvme_disable_device(nvme_device_data_t *nvme) {
    NVME_REG4(nvme->base, NVME_CC_OFFSET) &= ~(1UL);
    while ((NVME_REG4(nvme->base, NVME_CSTS_OFFSET) & 1) == 1);
    __sync_synchronize();
}

static void nvme_enable_device(nvme_device_data_t *nvme) {
    const uint32_t cc = 1 | (6 << 16) | (4 << 20);
    NVME_REG4(nvme->base, NVME_CC_OFFSET) = cc;
    while ((NVME_REG4(nvme->base, NVME_CSTS_OFFSET) & 1) == 0);
    __sync_synchronize();
}

static void nvme_do_one_cmd_synchronous(nvme_device_data_t *nvme, nvme_queue_t *queue) {
    queue->submission_queue_tail++;
    if (queue->submission_queue_tail > (queue->queue_size - 1))
        queue->submission_queue_tail = 0;
    
    NVME_REG4(nvme->base, NVME_SQTDBL_OFFSET(queue->queue_index, NVME_CAP_DSTRD(nvme->cap))) =
        queue->submission_queue_tail;

    uint32_t left_commands;
    if (queue->completion_queue_head < queue->submission_queue_tail)
        left_commands = queue->submission_queue_tail - queue->completion_queue_head;
    else
        left_commands = (queue->queue_size - queue->completion_queue_head) +
                        queue->submission_queue_tail;

    while (left_commands--) {
        volatile nvme_cq_entry_t *cq = &queue->completion_queue[queue->completion_queue_head];
        while ((cq->flags & 0x1) == queue->completion_queue_current_phase);
        
        queue->completion_queue_head++;
        if (queue->completion_queue_head > (queue->queue_size - 1)) {
            queue->completion_queue_head = 0;
            queue->completion_queue_current_phase ^= 1;
        }
    }

    NVME_REG4(nvme->base, NVME_CQHDBL_OFFSET(queue->queue_index, NVME_CAP_DSTRD(nvme->cap))) =
        queue->completion_queue_head;
}

static void nvme_create_io_queue(nvme_device_data_t *nvme) {
    volatile nvme_sq_entry_t *sq;
    
    // Set features - number of queues
    sq = &nvme->admin_queue.submission_queue[nvme->admin_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 9; // SETFEATURES
    sq->cid = NEXT_CID(nvme);
    sq->cdw10 = 7; // Number of queues
    sq->cdw11 = 0; // 1 queue (0-based)
    nvme_do_one_cmd_synchronous(nvme, &nvme->admin_queue);
    
    // Create IO completion queue
    sq = &nvme->admin_queue.submission_queue[nvme->admin_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 5; // CREATE IO CQ
    sq->cid = NEXT_CID(nvme);
    sq->prp[0] = V2P(nvme->io_queue.completion_queue);
    sq->cdw11 = 1; // PC bit
    sq->cdw10 = (nvme->io_queue.queue_index) | ((NVME_IO_QUEUE_SIZE - 1) << 16);
    nvme_do_one_cmd_synchronous(nvme, &nvme->admin_queue);
    
    // Create IO submission queue
    sq = &nvme->admin_queue.submission_queue[nvme->admin_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 1; // CREATE IO SQ
    sq->cid = NEXT_CID(nvme);
    sq->prp[0] = V2P(nvme->io_queue.submission_queue);
    sq->cdw11 = 1 | (nvme->io_queue.queue_index << 16);
    sq->cdw10 = (nvme->io_queue.queue_index) | ((NVME_IO_QUEUE_SIZE - 1) << 16);
    nvme_do_one_cmd_synchronous(nvme, &nvme->admin_queue);
}

static void nvme_identify_namespace(nvme_device_data_t *nvme) {
    nvme_namespace_data_t *ns_data = kalloc();
    if (!ns_data) return;
    
    volatile nvme_sq_entry_t *sq =
        &nvme->admin_queue.submission_queue[nvme->admin_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 6; // IDENTIFY
    sq->cid = NEXT_CID(nvme);
    sq->cdw10 = 0; // Namespace identify
    sq->nsid = NVME_NAMESPACE_INDEX;
    sq->prp[0] = V2P(ns_data);
    nvme_do_one_cmd_synchronous(nvme, &nvme->admin_queue);
    
    nvme->block_size = 2 << (ns_data->lba_format[ns_data->flbas & 0xF].lbads - 1);
    nvme->total_blocks = ns_data->nsze;
    
    kfree(ns_data);
}

// Driver operations
static int nvme_probe(device_t *dev) {
    pci_device_info_t *pci_info = (pci_device_info_t *)dev->os_data;
    
    // Check if this is an NVMe controller
    if (pci_info->class_code != 0x01 || pci_info->subclass != 0x08 || pci_info->prog_if != 0x02)
        return -1;
    
    ktprintf("[NVME_DRIVER] Probing NVMe controller (vendor=0x%x device=0x%x)\n",
             pci_info->vendor_id, pci_info->device_id);
    
    return 0;
}

static int nvme_init(device_t *dev) {
    pci_device_info_t *pci_info = (pci_device_info_t *)dev->os_data;
    
    // Get BAR0/BAR1 (64-bit address)
    uint64_t bar_addr = (((uint64_t)pci_info->bar[1] << 32) | (pci_info->bar[0] & 0xFFFFFFF0));
    
    ktprintf("[NVME_DRIVER] Initializing NVMe at BAR %p\n", bar_addr);
    
    // Allocate device data
    nvme_device_data_t *nvme = kcmalloc(sizeof(nvme_device_data_t));
    if (!nvme) return -1;
    
    // Map MMIO region
    nvme->base = vmm_io_memmap(bar_addr, 0x2000);
    if (!nvme->base) {
        kmfree(nvme);
        return -1;
    }
    
    nvme->next_command_id = 0;
    nvme->cap = NVME_REG8(nvme->base, NVME_CAP_OFFSET);
    
    // Allocate queues
    nvme->admin_queue.submission_queue = kcalloc();
    nvme->admin_queue.completion_queue = kcalloc();
    nvme->io_queue.submission_queue = kcalloc();
    nvme->io_queue.completion_queue = kcalloc();
    
    if (!nvme->admin_queue.submission_queue || !nvme->admin_queue.completion_queue ||
        !nvme->io_queue.submission_queue || !nvme->io_queue.completion_queue) {
        kmfree(nvme);
        return -1;
    }
    
    nvme->admin_queue.queue_index = 0;
    nvme->admin_queue.queue_size = NVME_ADMIN_QUEUE_SIZE;
    nvme->admin_queue.completion_queue_current_phase = 0;
    nvme->admin_queue.submission_queue_tail = 0;
    nvme->admin_queue.completion_queue_head = 0;
    
    nvme->io_queue.queue_index = 1;
    nvme->io_queue.queue_size = NVME_IO_QUEUE_SIZE;
    nvme->io_queue.completion_queue_current_phase = 0;
    nvme->io_queue.submission_queue_tail = 0;
    nvme->io_queue.completion_queue_head = 0;
    
    // Disable and configure controller
    nvme_disable_device(nvme);
    
    const uint32_t aqa = (NVME_ADMIN_QUEUE_SIZE - 1) | ((NVME_ADMIN_QUEUE_SIZE - 1) << 16);
    NVME_REG4(nvme->base, NVME_AQA_OFFSET) = aqa;
    NVME_REG8(nvme->base, NVME_ASQ_OFFSET) = V2P(nvme->admin_queue.submission_queue);
    NVME_REG8(nvme->base, NVME_ACQ_OFFSET) = V2P(nvme->admin_queue.completion_queue);
    
    nvme_enable_device(nvme);
    nvme_create_io_queue(nvme);
    nvme_identify_namespace(nvme);
    
    dev->driver_data = nvme;
    
    // Allocate and assign unique device name
    uint32_t device_id = __atomic_fetch_add(&g_nvme_device_count, 1, __ATOMIC_SEQ_CST);
    char *device_name = kcmalloc(16);  // Enough for "nvme" + max uint32 digits
    if (device_name) {
        snprintf(device_name, 16, "nvme%u", device_id);
        dev->name = device_name;
    } else {
        dev->name = "nvme?";  // Fallback if allocation fails
    }
    
    ktprintf("[NVME_DRIVER] %s ready: %llu blocks of %u bytes (%llu MB)\n",
             dev->name, nvme->total_blocks, nvme->block_size,
             (nvme->total_blocks * nvme->block_size) / (1024 * 1024));
    
    return 0;
}

static int nvme_read_op(device_t *dev, void *buffer, size_t count) {
    nvme_device_data_t *nvme = (nvme_device_data_t *)dev->driver_data;
    // TODO: Implement block read with LBA from buffer context
    return 0;
}

static int nvme_write_op(device_t *dev, const void *buffer, size_t count) {
    nvme_device_data_t *nvme = (nvme_device_data_t *)dev->driver_data;
    // TODO: Implement block write with LBA from buffer context
    return 0;
}

static int nvme_remove_op(device_t *dev)
{
    if (!dev)
        return 0;
    kfree((void*)dev->name);
    return 0;
}

static const driver_ops_t nvme_ops = {
    .probe = nvme_probe,
    .init = nvme_init,
    .read = nvme_read_op,
    .write = nvme_write_op,
    .remove = nvme_remove_op,
    .ioctl = NULL,
    .irq_handler = NULL
};

static driver_t nvme_driver = {
    .name = "nvme",
    .bus = DRIVER_BUS_PCI,
    .class_ = DRIVER_CLASS_BLOCK,
    .ops = nvme_ops,
    .priv = NULL,
    .manifest = NULL
};

void register_nvme_driver(void) {
    driver_register_verified(&nvme_driver);
}

// Legacy compatibility functions for existing FS code
static nvme_device_data_t *g_nvme = NULL;

void nvme_set_global(device_t *dev) {
    g_nvme = (nvme_device_data_t *)dev->driver_data;
}

void nvme_write(uint64_t lba, uint32_t block_count, const char *buffer) {
    if (!g_nvme) return;
    
    if (block_count * g_nvme->block_size > PAGE_SIZE) return;
    
    char *aligned_buffer = kalloc();
    memcpy(aligned_buffer, buffer, block_count * g_nvme->block_size);
    
    volatile nvme_sq_entry_t *sq =
        &g_nvme->io_queue.submission_queue[g_nvme->io_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 1; // WRITE
    sq->cid = NEXT_CID(g_nvme);
    sq->nsid = NVME_NAMESPACE_INDEX;
    sq->cdw10 = lba;
    sq->cdw11 = (lba >> 32);
    sq->cdw12 = (block_count - 1) & 0xFFFF;
    sq->prp[0] = V2P(aligned_buffer);
    
    nvme_do_one_cmd_synchronous(g_nvme, &g_nvme->io_queue);
    kfree(aligned_buffer);
}

void nvme_read(uint64_t lba, uint32_t block_count, char *buffer) {
    if (!g_nvme) return;
    
    if (block_count * g_nvme->block_size > PAGE_SIZE) return;
    
    char *aligned_buffer = kalloc();
    
    volatile nvme_sq_entry_t *sq =
        &g_nvme->io_queue.submission_queue[g_nvme->io_queue.submission_queue_tail];
    memset((void *)sq, 0, sizeof(nvme_sq_entry_t));
    sq->opc = 2; // READ
    sq->cid = NEXT_CID(g_nvme);
    sq->nsid = NVME_NAMESPACE_INDEX;
    sq->cdw10 = lba;
    sq->cdw11 = (lba >> 32);
    sq->cdw12 = block_count - 1;
    sq->prp[0] = V2P(aligned_buffer);
    
    nvme_do_one_cmd_synchronous(g_nvme, &g_nvme->io_queue);
    memcpy(buffer, aligned_buffer, block_count * g_nvme->block_size);
    kfree(aligned_buffer);
}

uint32_t nvme_block_size(void) {
    return g_nvme ? g_nvme->block_size : 512;
}