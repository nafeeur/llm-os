#ifndef LLMOS_VIRTIO_BLK_H
#define LLMOS_VIRTIO_BLK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Transport-independent legacy VirtIO block driver. A platform-specific
 * discovery file (arch/x86_64/virtio_pci.c for legacy virtio-pci,
 * arch/aarch64/virtio_mmio.c for legacy virtio-mmio) finds the device and
 * hands this module a small register-access vtable; everything about
 * virtqueue layout, request construction and polling for completion lives
 * here so it is shared, per the "hardware differences stop at the platform
 * layer" design invariant.
 *
 * Only the legacy (version-1) interface is implemented: a single split
 * virtqueue, no indirect descriptors, no event index, synchronous
 * busy-polled completion (there is no interrupt path yet -- see
 * docs/ROADMAP.md P3).
 */

typedef struct {
    void *ctx;
    uint32_t (*get_features)(void *ctx);
    void (*set_features)(void *ctx, uint32_t features);
    uint16_t (*queue_max_size)(void *ctx, uint16_t index);
    /* pfn is the physical page number of the virtqueue memory; queue_align
     * is the required alignment of the used ring (always 4096 here). */
    void (*set_queue)(void *ctx, uint16_t index, uint32_t pfn, uint16_t size, uint32_t queue_align);
    void (*notify)(void *ctx, uint16_t index);
    uint8_t (*get_status)(void *ctx);
    void (*set_status)(void *ctx, uint8_t status);
    void (*read_config)(void *ctx, uint32_t offset, void *dst, uint32_t len);
} VirtioBlkTransport;

bool virtio_blk_attach(const VirtioBlkTransport *transport);
bool virtio_blk_present(void);
uint64_t virtio_blk_sector_count(void);
bool virtio_blk_read(uint64_t lba, void *buf, uint32_t sector_count);

#endif
