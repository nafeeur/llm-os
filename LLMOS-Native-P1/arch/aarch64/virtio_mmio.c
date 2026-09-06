#include "llmos/platform.h"
#include "llmos/virtio_blk.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Legacy virtio-mmio discovery for the QEMU "virt" machine. QEMU exposes a
 * fixed bank of MMIO transport slots starting at 0x0a000000, 0x200 bytes
 * apart; unused slots read MagicValue but DeviceID 0. Requires the guest
 * launch to force the legacy (version 1) transport -- see
 * scripts/run-arm64.sh's "-global virtio-mmio.force-legacy=true" -- because
 * this driver does not implement the modern (version 2) register layout.
 */

#define VIRTIO_MMIO_BASE   0x0a000000ull
#define VIRTIO_MMIO_STRIDE 0x200u
#define VIRTIO_MMIO_SLOTS  32u

#define REG_MAGIC_VALUE        0x000u
#define REG_VERSION             0x004u
#define REG_DEVICE_ID           0x008u
#define REG_DEVICE_FEATURES     0x010u
#define REG_DEVICE_FEATURES_SEL 0x014u
#define REG_DRIVER_FEATURES     0x020u
#define REG_DRIVER_FEATURES_SEL 0x024u
#define REG_GUEST_PAGE_SIZE     0x028u
#define REG_QUEUE_SEL           0x030u
#define REG_QUEUE_NUM_MAX       0x034u
#define REG_QUEUE_NUM           0x038u
#define REG_QUEUE_ALIGN         0x03cu
#define REG_QUEUE_PFN           0x040u
#define REG_QUEUE_NOTIFY        0x050u
#define REG_STATUS              0x070u
#define REG_CONFIG              0x100u

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_ID_BLOCK 2u

static uint64_t mmio_base;

static uint32_t mmio_read32(uint64_t off) {
    return *(volatile uint32_t *)(uintptr_t)(mmio_base + off);
}
static void mmio_write32(uint64_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(mmio_base + off) = v;
}

static uint32_t t_get_features(void *ctx) {
    (void)ctx;
    mmio_write32(REG_DEVICE_FEATURES_SEL, 0);
    return mmio_read32(REG_DEVICE_FEATURES);
}
static void t_set_features(void *ctx, uint32_t f) {
    (void)ctx;
    mmio_write32(REG_DRIVER_FEATURES_SEL, 0);
    mmio_write32(REG_DRIVER_FEATURES, f);
}

static uint16_t t_queue_max_size(void *ctx, uint16_t index) {
    (void)ctx;
    mmio_write32(REG_QUEUE_SEL, index);
    return (uint16_t)mmio_read32(REG_QUEUE_NUM_MAX);
}

static void t_set_queue(void *ctx, uint16_t index, uint32_t pfn, uint16_t size, uint32_t queue_align) {
    (void)ctx;
    mmio_write32(REG_GUEST_PAGE_SIZE, 4096u);
    mmio_write32(REG_QUEUE_SEL, index);
    mmio_write32(REG_QUEUE_NUM, size);
    mmio_write32(REG_QUEUE_ALIGN, queue_align);
    mmio_write32(REG_QUEUE_PFN, pfn);
}

static void t_notify(void *ctx, uint16_t index) {
    (void)ctx;
    mmio_write32(REG_QUEUE_NOTIFY, index);
}

static uint8_t t_get_status(void *ctx) { (void)ctx; return (uint8_t)mmio_read32(REG_STATUS); }
static void t_set_status(void *ctx, uint8_t status) { (void)ctx; mmio_write32(REG_STATUS, status); }

static void t_read_config(void *ctx, uint32_t offset, void *dst, uint32_t len) {
    (void)ctx;
    volatile uint8_t *src = (volatile uint8_t *)(uintptr_t)(mmio_base + REG_CONFIG + offset);
    uint8_t *out = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; ++i) out[i] = src[i];
}

static bool discovery_done;
static bool discovery_ok;

static void discover_and_attach(void) {
    discovery_done = true;
    for (uint32_t slot = 0; slot < VIRTIO_MMIO_SLOTS; ++slot) {
        mmio_base = VIRTIO_MMIO_BASE + (uint64_t)slot * VIRTIO_MMIO_STRIDE;
        if (mmio_read32(REG_MAGIC_VALUE) != VIRTIO_MAGIC) continue;
        if (mmio_read32(REG_DEVICE_ID) != VIRTIO_DEVICE_ID_BLOCK) continue;

        VirtioBlkTransport xport = {
            .ctx = NULL,
            .get_features = t_get_features,
            .set_features = t_set_features,
            .queue_max_size = t_queue_max_size,
            .set_queue = t_set_queue,
            .notify = t_notify,
            .get_status = t_get_status,
            .set_status = t_set_status,
            .read_config = t_read_config,
        };
        discovery_ok = virtio_blk_attach(&xport);
        if (discovery_ok) return;
    }
}

bool platform_block_present(void) {
    if (!discovery_done) discover_and_attach();
    return discovery_ok && virtio_blk_present();
}

uint64_t platform_block_sector_count(void) {
    if (!discovery_done) discover_and_attach();
    return virtio_blk_sector_count();
}

bool platform_block_read(uint64_t lba, void *buf, uint32_t sector_count) {
    if (!discovery_done) discover_and_attach();
    return virtio_blk_read(lba, buf, sector_count);
}
