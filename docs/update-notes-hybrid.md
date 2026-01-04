# Hybrid Shell & Filesystem Guide

This document explains how the hybrid filesystem model works, how the shell is expected to interact with it, and what to watch for when balancing RAM-backed versus persistent data.

## Layout Summary

The virtual filesystem boots with several mount points:

- `/System`, `/Users`, `/Apps`, `/Temp`, and `/Volumes` are RAMFS volumes.
- `/Devices` is served by the device filesystem and exposes hardware aliases.
- `/Volumes/Disk0` (and optional `DiskN` clones) are FAT-backed mounts taken from the boot media.

The root directory remains a RAMFS shim that routes lookups into whichever mount owns the requested path prefix.

## RAMFS Behavior

RAMFS provides volatile storage. Key characteristics:

- Nested directories are supported, and directory removal cascades to children.
- Paths are normalized, so repeated slashes or `..` segments are collapsed before lookup.
- Because content resides in memory, everything under the RAMFS mounts disappears after a reboot unless an alias forwards the path into a persistent volume.

## FAT Volumes and Aliases

During boot the kernel mounts the FAT boot image under `/Volumes/Disk0`, seeds the directory tree `/Volumes/Disk0/Users/pran/Documents`, and registers a VFS alias that maps `/Users` onto `/Volumes/Disk0/Users`.

Alias properties:

- Every VFS call (open/read/write/list/mkdir/remove) resolves through the alias table before mount dispatch.
- Shell commands automatically benefit from alias resolution, so working under `/Users/pran` transparently targets the FAT volume.
- If an alias fails to register, `/Users` falls back to the volatile RAMFS; `whereami` exposes which mount is backing the current directory.

## Shell Command Reference

| Command | Description |
|---------|-------------|
| `ls [path]` | Lists directory contents; accepts absolute, relative, or aliased paths. |
| `tree [path]` | Recursively prints a directory tree with depth limiting to avoid runaway traversal. |
| `cd [path]` | Changes the working directory; resolving empty input echoes the current path. |
| `whereami` | Reports the mount backing the current directory and the resolved path inside that mount. |
| `volumes` | Dumps volume manager records (Disk identifiers and mount destinations). |
| `mount` | Lists all known VFS mount points, including RAMFS volumes and FAT mounts. |
| `cat <file>` | Streams file contents using the descriptor-based VFS API. |
| `touch <path>` | Creates or updates files; refuses to touch directories. |
| `mkdir <path>` | Creates directories at any depth; validates parent directories. |
| `rm <path>` | Removes files or directory trees recursively (use with care). |

## Working with Persistence

1. Confirm mounts: run `volumes` and `mount` to ensure `/Users` resolves to `Disk0` via the alias.
2. Create data: `echo "Hello" > /Users/pran/Documents/demo.txt`.
3. Verify write: `cat /Users/pran/Documents/demo.txt`.
4. Reboot with `reboot` and repeat the `cat` command. If the file vanished, the emulator may be using a snapshot or read-only disk.

Persistence depends on BIOS fallback writes reaching the underlying disk. Ensure QEMU runs without `-snapshot` and that the disk image allows writes.

## Graphics Console Primer

`vbe_init` validates framebuffer metadata, chooses a font (BIOS-provided, embedded BDF asset, or `font.psf` on Disk0), and redraws cached characters after every boot. If the display is unreadable, check boot logs for framebuffer diagnostics and confirm a usable font asset is present.

## Troubleshooting

- **Missing files after reboot**: confirm `/Users` points to `Disk0` with `whereami`; inspect emulator disk settings.
- **Command reports "path not found"**: run `tree /Volumes` to see if the FAT mount succeeded; if not, review boot logs for FAT initialization errors.
- **Garbled console**: verify that the embedded font asset exists and that `vbe_init` recorded a non-zero pitch and framebuffer size.
