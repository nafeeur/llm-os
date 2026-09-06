#include "llmos/platform.h"
#include "llmos/virtio_blk.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Legacy virtio-pci discovery for the QEMU "pc" machine. Scans PCI bus 0
 * (this machine has no bridges to recurse through), finds a transitional
 * virtio-blk device (vendor 0x1af4, device 0x1001) and drives it through
 * its legacy I/O-port register block. Virtqueue mechanics live in
 * src/virtio_blk.c; this file only knows how to read/write those registers.
 */

#define PCI_CONFIG_ADDRESS 0xcf8u
#define PCI_CONFIG_DATA    0xcfcu
#define VIRTIO_VENDOR_ID   0x1af4u
#define VIRTIO_BLK_DEVICE_ID 0x1001u

#define VIRTIO_PCI_REG_DEVICE_FEATURES 0x00u
#define VIRTIO_PCI_REG_GUEST_FEATURES  0x04u
#define VIRTIO_PCI_REG_QUEUE_ADDRESS   0x08u
#define VIRTIO_PCI_REG_QUEUE_SIZE      0x0cu
#define VIRTIO_PCI_REG_QUEUE_SELECT    0x0eu
#define VIRTIO_PCI_REG_QUEUE_NOTIFY    0x10u
#define VIRTIO_PCI_REG_STATUS          0x12u
#define VIRTIO_PCI_REG_ISR             0x13u
#define VIRTIO_PCI_REG_CONFIG          0x14u

static inline uint8_t io_in8(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void io_out8(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t io_in16(uint16_t port) {
    uint16_t v; __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void io_out16(uint16_t port, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t io_in32(uint16_t port) {
    uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void io_out32(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

static uint32_t pci_address(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)func << 8) | (uint32_t)(offset & 0xfcu);
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, dev, func, offset));
    return io_in32(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, dev, func, offset));
    io_out32(PCI_CONFIG_DATA, value);
}

static uint16_t io_base; /* BAR0 I/O port base of the discovered device */

static uint32_t t_get_features(void *ctx) { (void)ctx; return io_in32(io_base + VIRTIO_PCI_REG_DEVICE_FEATURES); }
static void t_set_features(void *ctx, uint32_t f) { (void)ctx; io_out32(io_base + VIRTIO_PCI_REG_GUEST_FEATURES, f); }

static uint16_t t_queue_max_size(void *ctx, uint16_t index) {
    (void)ctx;
    io_out16(io_base + VIRTIO_PCI_REG_QUEUE_SELECT, index);
    return io_in16(io_base + VIRTIO_PCI_REG_QUEUE_SIZE);
}

static void t_set_queue(void *ctx, uint16_t index, uint32_t pfn, uint16_t size, uint32_t queue_align) {
    (void)ctx; (void)size; (void)queue_align;
    io_out16(io_base + VIRTIO_PCI_REG_QUEUE_SELECT, index);
    io_out32(io_base + VIRTIO_PCI_REG_QUEUE_ADDRESS, pfn);
}

static void t_notify(void *ctx, uint16_t index) {
    (void)ctx;
    io_out16(io_base + VIRTIO_PCI_REG_QUEUE_NOTIFY, index);
}

static uint8_t t_get_status(void *ctx) { (void)ctx; return io_in8(io_base + VIRTIO_PCI_REG_STATUS); }
static void t_set_status(void *ctx, uint8_t status) { (void)ctx; io_out8(io_base + VIRTIO_PCI_REG_STATUS, status); }

static void t_read_config(void *ctx, uint32_t offset, void *dst, uint32_t len) {
    (void)ctx;
    uint8_t *out = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; ++i)
        out[i] = io_in8((uint16_t)(io_base + VIRTIO_PCI_REG_CONFIG + offset + i));
}

static bool discovery_done;
static bool discovery_ok;

static void discover_and_attach(void) {
    discovery_done = true;
    for (uint16_t dev = 0; dev < 32u; ++dev) {
        uint32_t id = pci_read32(0, (uint8_t)dev, 0, 0x00u);
        uint16_t vendor = (uint16_t)(id & 0xffffu);
        uint16_t device = (uint16_t)(id >> 16);
        if (vendor != VIRTIO_VENDOR_ID || device != VIRTIO_BLK_DEVICE_ID) continue;

        uint32_t command_status = pci_read32(0, (uint8_t)dev, 0, 0x04u);
        pci_write32(0, (uint8_t)dev, 0, 0x04u, command_status | 0x1u | 0x4u); /* I/O space + bus master */

        uint32_t bar0 = pci_read32(0, (uint8_t)dev, 0, 0x10u);
        if (!(bar0 & 0x1u)) continue; /* must be an I/O BAR */
        io_base = (uint16_t)(bar0 & 0xfffcu);

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
        return;
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
