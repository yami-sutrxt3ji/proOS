# Phase 3 Test Plan

## Build Checks

1. Run `make`.
   - Expect `build/proos.img` with no errors.
2. Confirm boot stages sized correctly:
   ```bash
   ls -l build/mbr.bin build/stage2.bin build/kernel.bin
   ```
   - `mbr.bin` should be 512 bytes.
   - `stage2.bin` stays 2048 bytes (4 sectors).
   - `kernel.bin` increases to include scheduler, syscall, and user binaries.
3. Optional: `hexdump -C build/mbr.bin | head` to verify the `55 aa` signature.

## QEMU Functional Test

1. Launch with `make run-qemu`.
2. Observe boot output:
   - Banner `proOS — PSEK (Protected Mode)`.
   - `init` prints `init: starting echo service` followed by `ECHO: Hello` (user IPC demo).
   - Shell prompt `proos>` appears afterward.
3. Shell regression checks:
   - `help` → lists `proc_list` alongside previous commands.
   - `proc_list` → shows active processes (expect echo service in WAITING state).
   - `ls`, `cat hello.txt`, `echo testing > new.txt`, `cat new.txt` → RAMFS behaviour unchanged.
   - `mem` → uptime increases between invocations.
4. Issue `reboot` → VM resets (or halts if reset unsupported).

## VirtualBox Spot Check

1. Convert the raw image: `VBoxManage convertfromraw build/proos.img build/proos.vdi --format VDI`.
2. Boot the VM and confirm the same `ECHO: Hello` banner plus shell behaviour.

## ISO Boot (Optional)

1. Run `make iso`.
2. Launch with `qemu-system-i386 -cdrom build/proos.iso` and repeat the checks above.

## Debug Tips

- If the scheduler appears idle, ensure `process_create` returns a valid PID (rebuild with `make clean && make`).
- Use `proc_list` to inspect PCB states (READY/RUNNING/WAITING/ZOMBIE).
- Add temporary prints in `syscall_handler` when debugging syscall paths.
- `qemu-system-i386 -d cpu_reset,int` helps trace faults around context switches or syscalls.
