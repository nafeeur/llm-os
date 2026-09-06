#include "llmos/virtio_blk.h"
#include "llmos/platform.h"

#define VQ_QUEUE_INDEX     0u
#define VQ_MAX_SIZE        256u  /* compile-time cap on the buffer below; the
                                   * legacy interface reports QueueSize as a
                                   * read-only device property (commonly 128
                                   * or 256 in QEMU) -- the driver cannot ask
                                   * for a smaller ring, so the memory layout
                                   * must match whatever size the device
                                   * reports, up to this cap. */
#define VQ_ALIGN           4096u
#define VQ_PAGE_SIZE       4096u

#define VIRTQ_DESC_F_NEXT  1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER      2u
#define VIRTIO_STATUS_DRIVER_OK   4u

#define VIRTIO_BLK_T_IN  0u
#define VIRTIO_BLK_S_OK  0u
#define SECTOR_BYTES     512u

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VirtqDesc;

typedef struct {
    uint32_t id;
    uint32_t len;
} VirtqUsedElem;

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} VirtioBlkReqHeader;

/* Worst-case layout for VQ_MAX_SIZE descriptors: desc table (16 bytes each)
 * + avail ring (4 + 2*n bytes), padded up to VQ_ALIGN, then the used ring
 * (4 + 8*n bytes). Sized generously and rounded up to whole pages. */
#define VQ_MEMORY_BYTES (3u * VQ_PAGE_SIZE)
static uint8_t vq_memory[VQ_MEMORY_BYTES] __attribute__((aligned(VQ_PAGE_SIZE)));

static VirtioBlkReqHeader req_header __attribute__((aligned(16)));
static uint8_t req_status __attribute__((aligned(8)));

static VirtioBlkTransport transport;
static bool attached;
static uint64_t sector_count;
static uint16_t used_idx_seen;
static uint16_t vq_size;
static uint32_t vq_avail_offset;
static uint32_t vq_used_offset;

static VirtqDesc *vq_desc(void) { return (VirtqDesc *)vq_memory; }
static uint16_t *vq_avail_idx(void) { return (uint16_t *)(vq_memory + vq_avail_offset + 2u); }
static uint16_t *vq_avail_ring(void) { return (uint16_t *)(vq_memory + vq_avail_offset + 4u); }
static uint16_t *vq_used_idx(void) { return (uint16_t *)(vq_memory + vq_used_offset + 2u); }

static uint32_t align_up(uint32_t v, uint32_t align) { return (v + align - 1u) & ~(align - 1u); }

bool virtio_blk_attach(const VirtioBlkTransport *xport) {
    attached = false;
    transport = *xport;

    transport.set_status(transport.ctx, 0); /* reset */
    transport.set_status(transport.ctx, VIRTIO_STATUS_ACKNOWLEDGE);
    transport.set_status(transport.ctx, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    (void)transport.get_features(transport.ctx);
    transport.set_features(transport.ctx, 0u); /* no optional features negotiated */

    uint16_t reported = transport.queue_max_size(transport.ctx, VQ_QUEUE_INDEX);
    if (reported == 0u || reported > VQ_MAX_SIZE) return false;
    vq_size = reported;

    uint32_t desc_bytes = (uint32_t)vq_size * (uint32_t)sizeof(VirtqDesc);
    vq_avail_offset = desc_bytes;
    uint32_t avail_bytes = 4u + 2u * (uint32_t)vq_size;
    vq_used_offset = align_up(vq_avail_offset + avail_bytes, VQ_ALIGN);
    uint32_t used_bytes = 4u + (uint32_t)sizeof(VirtqUsedElem) * (uint32_t)vq_size;
    if (vq_used_offset + used_bytes > VQ_MEMORY_BYTES) return false;

    for (uint32_t i = 0; i < sizeof(vq_memory); ++i) vq_memory[i] = 0;
    uint32_t pfn = (uint32_t)((uintptr_t)vq_memory / VQ_PAGE_SIZE);
    transport.set_queue(transport.ctx, VQ_QUEUE_INDEX, pfn, vq_size, VQ_ALIGN);

    uint64_t capacity = 0;
    transport.read_config(transport.ctx, 0u, &capacity, sizeof(capacity));
    sector_count = capacity;
    used_idx_seen = 0;

    transport.set_status(transport.ctx,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    attached = sector_count > 0;
    return attached;
}

bool virtio_blk_present(void) { return attached; }
uint64_t virtio_blk_sector_count(void) { return sector_count; }

bool virtio_blk_read(uint64_t lba, void *buf, uint32_t count) {
    if (!attached || !buf || !count) return false;
    if (lba + count > sector_count) return false;

    req_header.type = VIRTIO_BLK_T_IN;
    req_header.reserved = 0;
    req_header.sector = lba;
    req_status = 0xffu;

    VirtqDesc *desc = vq_desc();
    desc[0].addr = (uint64_t)(uintptr_t)&req_header;
    desc[0].len = (uint32_t)sizeof(req_header);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uint64_t)(uintptr_t)buf;
    desc[1].len = count * SECTOR_BYTES;
    desc[1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    desc[1].next = 2;

    desc[2].addr = (uint64_t)(uintptr_t)&req_status;
    desc[2].len = 1;
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0;

    uint16_t *avail_idx = vq_avail_idx();
    uint16_t *avail_ring = vq_avail_ring();
    uint16_t slot = (uint16_t)(*avail_idx % vq_size);
    avail_ring[slot] = 0;
    __asm__ volatile ("" ::: "memory");
    *avail_idx = (uint16_t)(*avail_idx + 1u);
    __asm__ volatile ("" ::: "memory");

    transport.notify(transport.ctx, VQ_QUEUE_INDEX);

    uint16_t *used_idx = vq_used_idx();
    uint64_t spins = 0;
    const uint64_t spin_limit = 200000000ull;
    while (*used_idx == used_idx_seen) {
        platform_relax();
        if (++spins > spin_limit) return false; /* device did not respond */
    }
    used_idx_seen = (uint16_t)(used_idx_seen + 1u);

    return req_status == VIRTIO_BLK_S_OK;
}
