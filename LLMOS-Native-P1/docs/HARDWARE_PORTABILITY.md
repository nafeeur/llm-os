# Hardware portability

## What “works on x86-64 and Arm64” means

No nontrivial bare-metal OS can use one identical binary on every board. CPU
instruction sets may match while firmware, interrupt controllers, UARTs,
storage, PCIe, USB, clocks and power controllers differ.

LLMOS therefore uses:

```text
one shared model/agent/tensor core
+ one architecture layer per ISA
+ one small platform layer per firmware/board family
+ one operator pack per CPU/accelerator feature set
```

A board port must not reimplement model scheduling, KV management, agents,
capabilities or transactions.

## Preferred boot strategy

### x86-64

Production support should use UEFI to acquire the firmware memory map, ACPI,
framebuffer and loaded model-package location, then call `ExitBootServices()`.
The P1 BIOS image is intentionally a tiny development path for QEMU and legacy
VMs, not the final universal x86 boot path.

### Generic Arm64

Generic Arm64 machines should boot through UEFI or a firmware-supplied Device
Tree. The Device Tree describes board-specific device addresses and interrupts.
The QEMU `virt` P1 target currently uses the documented development layout and
receives the DTB pointer in `x0`; a production port will parse it instead of
hard-coding all peripherals.

### Raspberry Pi

Raspberry Pi firmware loads a board-specific 64-bit kernel image and passes a
merged Device Tree. The P1 image is linked for the Raspberry Pi 4 BCM2711 and
uses its PL011 UART directly.

Raspberry Pi 5 is not merely a faster Pi 4 port. BCM2712 and the RP1 I/O
controller require a separate platform driver, boot validation and interrupt
mapping. It belongs in `arch/aarch64/platform_pi5.c`, sharing all code above the
HAL.

## Current support matrix

| Target | Build artifact | Status |
|---|---|---|
| x86-64 QEMU/legacy BIOS | `build/x86_64/llmos-x86_64.img` | Compiled and structurally checked |
| Arm64 QEMU `virt` | `build/aarch64-qemu/llmos-aarch64-qemu.bin` | Compiled and structurally checked |
| Raspberry Pi 4 | `build/rpi4/llmos-rpi4.img` | Compiled; requires physical serial boot validation |
| x86-64 UEFI hardware | none yet | Required production port |
| Generic Arm64 UEFI hardware | none yet | Required production port |
| Raspberry Pi 5 | none yet | Separate BCM2712/RP1 port required |
| GPU/NPU accelerators | none yet | Device queue and native driver work required |

## Minimum useful small hardware

The kernel itself is small. Model capacity is dictated by the installed model,
quantization and context budget. LLMOS should boot with tens of MiB, but useful
local inference generally needs considerably more RAM. The admission controller
must reject a model or context before memory exhaustion rather than depending
on late generic swapping.
