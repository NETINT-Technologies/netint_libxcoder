# FreeBSD firmware upgrade tool for NETINT Quadra

FreeBSD's nvmecontrol(8) refuses firmware download on this card (Identify reports
no Firmware Activate/Download support), and NETINT's quadra_auto_upgrade.sh needs
nvme-cli (Linux). `nvmefw` reproduces the sequence that script sends, measured on
Linux with strace/ftrace:

1. controller reset
2. Firmware Image Download (admin 0x11), 4096 bytes per command,
   cdw10 = dwords - 1, cdw11 = byte offset / 4
3. vendor admin 0xC7 (no data): burn the image from the card's DDR to NOR flash (~70 s)
4. the new firmware runs after a power cycle (cold upgrade)

It uses the native NVME_PASSTHROUGH_CMD ioctl, not the Linux-compatible one: the
latter never returns the NVMe completion status, so a failed command would look
like success. It aborts before the burn if any download chunk fails.

## Build and use

    cc -O1 -Wall -o nvmefw nvmefw.c
    sudo ./quadra_fw_upgrade.sh -n /dev/nvme1 nor_flash_vX-Quadra.bin   # dry run
    sudo ./quadra_fw_upgrade.sh    /dev/nvme1 nor_flash_vX-Quadra.bin   # flash

`quadra_fw_upgrade.sh` checks vendor/model, image size and revision, that no
process has the card open, and raises dev.nvme.N.admin_timeout_period to 120 s
around the burn (default 60 s would reset the controller mid-burn).

## Status: read before flashing

- Tested on ONE card (QuadraT1U-EP3, FreeBSD 16.0-CURRENT) with a SAME-version
  re-flash (5906tFr1 to 5906tFr1): card came back, identify and ni_rsrc_list OK.
- A real version change (upgrade or downgrade) is NOT tested.
- Choosing the image is manual: NETINT ships one image per persistent
  configuration (-Quadra, -C, -E, -F, -G). Linux reads the letter from the card
  (opcode 0xC2 / log page 194); an old firmware rejects it and the plain
  -Quadra image applies.
- The tool writes flash. Do not lose power during the burn.
