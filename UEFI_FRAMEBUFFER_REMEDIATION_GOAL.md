# Goal: Make TestOS Display Reliably on Modern x86-64 UEFI Hardware

## Objective

Replace the current QEMU-oriented, 32-bit Multiboot framebuffer handoff with a
hardware-safe display path that can show stable TestOS boot diagnostics and a
ready screen on modern x86-64 UEFI laptops, including the HP Laptop 15-fd0083wm
(Intel N200 / Intel UHD).

The existing USB image and Limine loader have already been proven to launch the
kernel. The unresolved failure is after handoff: a modern UEFI GOP framebuffer
may reside above 4 GiB, while the current 32-bit kernel cannot address it and
falls back to legacy VGA, which is not a usable display interface on the target
laptop.

## Non-Goals

- Do not modify, install to, mount read-write, or otherwise write to the
  laptop's internal Windows disk.
- Do not claim Secure Boot support unless the complete chain is signed and
  tested.
- Do not treat a QEMU legacy-VGA boot as evidence that physical UEFI graphics
  work.
- Do not reflash a physical USB until the revised exact image passes the
  validation ladder and the user explicitly confirms the target disk identifier.

## Required Design Decision

Adopt a real x86-64 UEFI-capable execution and display path. The preferred
solution is to boot a 64-bit TestOS kernel (or a small, maintained 64-bit
kernel/display layer that owns the early boot handoff) through Limine's native
64-bit protocol, preserving all framebuffer addresses as 64-bit values.

A 32-bit PAE framebuffer aperture is acceptable only if it is fully designed,
implemented, and tested as a supported architecture. It must map the complete
framebuffer above 4 GiB into a documented virtual aperture without truncation.
Do not retain the current `framebuffer_addr > 0xffffffff` rejection plus
legacy-VGA fallback as the physical-hardware solution.

## Implementation Requirements

### 1. Boot and CPU State

- Select and document the kernel ABI, boot protocol, physical/virtual layout,
  and transition into 64-bit mode.
- Keep the standard removable-media loader at `EFI/BOOT/BOOTX64.EFI` and retain
  the reproducible GPT/FAT32 USB image workflow.
- At the earliest assembly entry, execute `cli` and `cld` before setting up the
  stack or entering C/C++ code.
- Validate every bootloader-provided structure, revision, size, address, and
  optional response before use.
- Preserve serial diagnostics for automated tests, but do not require serial
  hardware for physical success.

### 2. Framebuffer Ownership and Rendering

- Treat framebuffer physical addresses, pitch, dimensions, and sizes as
  64-bit-safe values; never silently truncate them to a 32-bit pointer.
- Map the entire framebuffer through the kernel's active paging scheme before
  rendering.
- Validate pixel format, bits per pixel, RGB mask sizes/offsets, pitch, and
  overflow conditions. On invalid data, show a stable error through any valid
  supported output and halt safely.
- Provide one canonical early-console implementation used by boot stages,
  normal kernel logging, warnings, and panics.
- Do not touch `0xB8000` or VGA CRTC I/O ports during a UEFI framebuffer boot.
  Legacy VGA may remain only as an explicit, separately selected BIOS-compatible
  path.
- Reserve the framebuffer and boot-information memory ranges in physical-memory
  accounting so later allocation cannot overwrite them.

### 3. Visible Diagnostics

Show a stable screen with a stage and short failure reason. At minimum cover:

1. Kernel entry
2. Boot information validated
3. Display mapped and initialized
4. Memory map accepted
5. Descriptor tables and paging ready
6. Interrupt/timer initialization complete
7. TestOS ready

Fatal failures must remain visible, use a stable error code, and halt without
rebooting or writing to storage.

### 4. Compatibility and Scope Control

- Keep existing TCP, socket, HTTP, Docker, and QEMU regression paths working.
- Do not weaken existing tests or replace an exact-image test with `-kernel`
  injection.
- Make migration changes focused and documented; leave unrelated subsystems
  unchanged.
- Pin any added bootloader, firmware, or build dependency with version and
  checksum/source verification.

## Validation Ladder

1. Build the image reproducibly through the documented Docker workflow.
2. Inspect the raw artifact: GPT, FAT32 ESP, `EFI/BOOT/BOOTX64.EFI`, bootloader
   configuration, kernel files, image size, and SHA-256.
3. Boot the exact `build/testos-usb.img` with QEMU and clean OVMF variables;
   do not use `-kernel`.
4. Add an automated UEFI graphics configuration that does **not** expose legacy
   VGA, and prove that the framebuffer diagnostic reaches `TestOS ready`.
5. Capture or otherwise automatically verify the visible framebuffer result,
   not solely serial output.
6. Repeat five cold boots at two RAM sizes, including a configuration where the
   framebuffer address is above 4 GiB when the emulator supports it.
7. Run existing QEMU, TCP, socket, and HTTP regression tests unchanged.
8. Document the final architecture, limitations, build command, checksum, and
   guarded macOS/Windows USB flashing procedures.
9. Only then, after read-only identification and explicit confirmation of the
   exact removable disk, flash the image and retest the HP through its UEFI USB
   boot option.

## Acceptance Criteria

The engineering work is complete when:

- The display path does not depend on legacy VGA during UEFI boot.
- A framebuffer above 4 GiB is either supported without address truncation or
  rejected before kernel launch with a documented, visible diagnostic; it is
  never silently redirected to an invisible VGA fallback.
- The exact removable-media image repeatedly reaches a visible `TestOS ready`
  state in UEFI virtual tests that omit legacy VGA.
- Existing regression suites remain green.
- On the HP laptop, the USB reaches and retains the ready screen without
  installing or writing to internal storage.
- After USB removal, Windows boots normally and unchanged.

## Evidence to Record

For each milestone, record the source revision, image SHA-256, QEMU command,
firmware configuration, observed boot-stage output, and test result. For the
physical test, record only the removable device identifier/model/capacity and
the user's explicit erase confirmation; never record credentials or modify the
internal disk.
