# proOS Shell

The proOS shell provides a text-based interface for interacting with the system once the kernel finishes booting. This guide covers the default commands and interactive behavior.

## Prompts and Input
- The shell prompt is `proOS >> `.
- Input is line buffered. Press Enter to submit the current line.
- Press Backspace to delete the most recently typed character.
- Press Tab to insert a space character.
- Press the Up Arrow to recall the previously executed command. Repeated presses walk backward through command history. Press the Down Arrow to move toward newer entries or clear the line once you reach the end of history.

## Built-in Commands
- `help` — Display available commands.
- `clear` — Clear the VGA text console.
- `echo` — Print a message or redirect output to a file.
- `mem` — Show memory usage and uptime.
- `memdump <addr> [len]` — Dump memory starting at a physical address.
- `reboot` — Issue a warm reboot.
- `ls` — List files in the RAM filesystem.
- `cat <path>` — Display file contents from RAMFS.
- `lsfs` — List files in the FAT16 image if present.
- `catfs <path>` — Display files from the FAT16 image.
- `mod` — Manage dynamic modules (list, load, unload).
- `gfx` — Render the compositor demo.
- `kdlg` — Dump recent kernel logs.
- `kdlvl [lvl]` — Adjust log verbosity.
- `tasks` — List running processes.
- `proc_count` — Report the process count.
- `spawn <n>` — Stress test process creation.
- `devs` — Display registered devices.
- `shutdown` — Power off using ACPI when available.

## Command History
- The shell keeps a rolling history of recent commands.
- Duplicate consecutive commands are not stored twice.
- History persists only for the current session.

## Extensibility
The shell is implemented in `kernel/shell.c`. New commands can be added by extending the dispatch logic in `shell_execute` and exporting helper routines for the target subsystem.
