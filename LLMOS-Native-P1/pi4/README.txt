Raspberry Pi 4 bare-metal test

1. Start from a Raspberry Pi boot FAT partition containing the normal firmware
   files (start4.elf, fixup4.dat and the board DTB files).
2. Copy build/rpi4/llmos-rpi4.img into the partition root.
3. Merge the supplied config.txt settings into the partition's config.txt.
4. Connect a 3.3 V USB-to-TTL serial adapter:
      GPIO14 / pin 8  -> adapter RX
      GPIO15 / pin 10 -> adapter TX
      GND / pin 6     -> adapter GND
   Do not connect a 5 V serial adapter.
5. Open 115200 8N1 serial and power on.

This P1 image targets BCM2711 / Raspberry Pi 4. Raspberry Pi 5 requires its own
BCM2712/RP1 platform driver and is not represented as validated by this image.
