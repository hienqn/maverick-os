#include "threads/loader.h"

#### Pintos Bootstrap Loader (Boot Sector)
####
#### PURPOSE: This 512-byte boot sector (loader.bin) is the first code to execute
#### when Pintos boots. It runs in 16-bit real mode and acts as a bridge between
#### BIOS and the Pintos kernel.
####
#### BOOT PROCESS:
#### 1. BIOS loads this sector (512 bytes) from disk sector 0 to memory at 0x7c00
#### 2. BIOS jumps to 0x7c00, loader begins executing (we're still in real mode)
#### 3. Loader scans all hard disks for Pintos kernel partition (type 0x20, bootable flag 0x80)
#### 4. Loads kernel.bin from the found partition into memory at 0x20000
#### 5. Reads kernel entry point from ELF header and jumps to it (start.S)
####
#### REAL MODE: CPU operates like an 8086 with 16-bit addressing, segment:offset 
#### memory access, no memory protection. The kernel will switch to protected mode.
####
#### DISK NUMBERING: DL register holds drive number (floppy: 0x00-0x7F, 
#### hard disks: 0x80-0xFF). We scan multiple drives to support flexible setups.

# Runs in real mode, which is a 16-bit segment.
	.code16

# Set up segment registers.
# Set stack to grow downward from 60 kB (after boot, the kernel
# continues to use this stack for its initial thread).

	sub %ax, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov $0xf000, %esp

# Configure serial port so we can report progress without connected VGA.
# See [IntrList] for details.
	sub %dx, %dx			# Serial port 0.
	mov $0xe3, %al			# 9600 bps, N-8-1.
					# AH is already 0 (Initialize Port).
	int $0x14			# Destroys AX.

	call puts
	.string "PiLo"

#### Read the partition table on each system hard disk and scan for a
#### partition of type 0x20, which is the type that we use for a
#### Pintos kernel.
####
#### Read [Partitions] for a description of the partition table format
#### that we parse.
####
#### We print out status messages to show the disk and partition being
#### scanned, e.g. hda1234 as we scan four partitions on the first
#### hard disk.

	mov $0x80, %dl			# Hard disk 0.
read_mbr:
	sub %ebx, %ebx			# EBX = 0 (read MBR, sector 0).
	mov $0x2000, %ax		# Buffer segment = 0x2000
	mov %ax, %es			# ES now points to physical 0x20000
	call read_sector		# Load disk's MBR (512 bytes) into memory at 0x20000
	jc no_such_drive		# If BIOS says no disk exists, try next disk

	# Print hd[a-z] to show which disk we're checking (e.g., "hda", "hdb").
	call puts
	.string " hd"
	mov %dl, %al			# DL = disk number (0x80, 0x81, ...)
	add $'a' - 0x80, %al	# Convert 0x80 → 'a', 0x81 → 'b', etc.
	call putc

	# MBR signature: bytes 510-511 must be 0xaa55 for valid partition table.
	# If not present, this isn't a partitioned disk (might be raw disk, USB, etc.)
	cmpw $0xaa55, %es:510
	jne next_drive

	mov $446, %si			# SI = 446 (offset of partition entry 1 in MBR)
	mov $'1', %al			# AL = '1' (partition number for display)
check_partition:
	# Check if partition is in use (first 4 bytes != 0 means it has data).
	cmpl $0, %es:(%si)
	je next_partition		# Empty partition slot, skip it

	# Print partition number being checked (1, 2, 3, or 4)
	call putc

	# Check partition type: 0x20 = Pintos kernel partition
	cmpb $0x20, %es:4(%si)		# Byte 4 of partition entry = partition type
	jne next_partition

	# Check bootable flag: 0x80 = marked as bootable
	cmpb $0x80, %es:(%si)		# Byte 0 of partition entry = boot flag
	je load_kernel

next_partition:
	# Advance to next partition entry (each entry is 16 bytes).
	add $16, %si			# SI += 16 (move to next partition entry)
	inc %al				# Increment partition number ('1'→'2'→'3'→'4')
	cmp $510, %si			# Have we checked all 4 partitions? (446+64=510)
	jb check_partition		# If SI < 510, check next partition

next_drive:
	# Try the next disk (0x80 → 0x81 → 0x82 ...)
	inc %dl				# Increment disk number
	jnc read_mbr			# If no overflow (CF=0), continue scanning

no_such_drive:
no_boot_partition:
	# Didn't find a Pintos kernel partition anywhere, give up.
	call puts
	.string "\rNot found\r"

	# Notify BIOS that boot failed.  See [IntrList].
	int $0x18

#### We found a kernel.  The kernel's drive is in DL.  The partition
#### table entry for the kernel's partition is at ES:SI.  Our job now
#### is to read the kernel from disk and jump to its start address.

load_kernel:
	call puts
	.string "\rLoading"		# Print "Loading" then dots as we load

	# Calculate how many sectors to read. ELF format doesn't have an easy size field.
	# We just read the entire partition (up to 512KB max, because of loader constraints).
	mov %es:12(%si), %ecx		# ECX = partition size (bytes 12-15 of partition entry)
	cmp $1024, %ecx			# Cap at 1024 sectors (512KB)
	jbe 1f
	mov $1024, %cx			# Limit to 512KB
1:

	mov %es:8(%si), %ebx		# EBX = starting sector number (bytes 8-11)
	mov $0x2000, %ax		# AX = 0x2000 (we'll load kernel at 0x20000)

next_sector:
	# Load one sector (512 bytes) from disk into memory
	mov %ax, %es			# ES = load address (incrementing each iteration)
	call read_sector		# Read sector EBX into memory at ES:0000
	jc read_failed			# If BIOS reports error, give up

	# Show progress: print '.' every 16 sectors (every 8 KB loaded)
	test $15, %bl			# Test if sector number is divisible by 16
	jnz 1f				# If not, skip printing
	call puts
	.string "."			# Print "." to show loading progress
1:

	# Move to next sector: advance both memory address and disk sector number
	add $0x20, %ax			# AX += 0x20 (move to next 512-byte block in memory)
	inc %bx				# BX++ (next sector on disk)
	loop next_sector		# Loop while ECX > 0 (decrements ECX)

	call puts
	.string "\r"

#### Transfer control to the kernel that we loaded.  We read the start
#### address out of the ELF header (see [ELF1]) and convert it from a
#### 32-bit linear address into a 16:16 segment:offset address for
#### real mode, then jump to the converted address.  The 80x86 doesn't
#### have an instruction to jump to an absolute segment:offset kept in
#### registers, so in fact we store the address in a temporary memory
#### location, then jump indirectly through that location.  To save 4
#### bytes in the loader, we reuse 4 bytes of the loader's code for
#### this temporary pointer.

	mov $0x2000, %ax
	mov %ax, %es
	mov %es:0x18, %dx
	mov %dx, start
	movw $0x2000, start + 2
	ljmp *start

read_failed:
start:
	# Disk sector read failed.
	call puts
1:	.string "\rBad read\r"

	# Notify BIOS that boot failed.  See [IntrList].
	int $0x18

#### Print string subroutine.  To save space in the loader, this
#### subroutine takes its null-terminated string argument from the
#### code stream just after the call, and then returns to the byte
#### just after the terminating null.  This subroutine preserves all
#### general-purpose registers.

puts:	xchg %si, %ss:(%esp)
	push %ax
next_char:
	mov %cs:(%si), %al
	inc %si
	test %al, %al
	jz 1f
	call putc
	jmp next_char
1:	pop %ax
	xchg %si, %ss:(%esp)
	ret

#### Character output subroutine.  Prints the character in AL to the
#### VGA display and serial port 0, using BIOS services (see
#### [IntrList]).  Preserves all general-purpose registers.
####
#### If called upon to output a carriage return, this subroutine
#### automatically supplies the following line feed.

putc:	pusha

1:	sub %bh, %bh			# Page 0.
	mov $0x0e, %ah			# Teletype output service.
	int $0x10

	mov $0x01, %ah			# Serial port output service.
	sub %dx, %dx			# Serial port 0.
2:	int $0x14			# Destroys AH.
	test $0x80, %ah			# Output timed out?
	jz 3f
	movw $0x9090, 2b		# Turn "int $0x14" above into NOPs.

3:
	cmp $'\r', %al
	jne popa_ret
	mov $'\n', %al
	jmp 1b

#### Sector read subroutine.  Takes a drive number in DL (0x80 = hard
#### disk 0, 0x81 = hard disk 1, ...) and a sector number in EBX, and
#### reads the specified sector into memory at ES:0000.  Returns with
#### carry set on error, clear otherwise.  Preserves all
#### general-purpose registers.

read_sector:
	pusha
	sub %ax, %ax
	push %ax			# LBA sector number [48:63]
	push %ax			# LBA sector number [32:47]
	push %ebx			# LBA sector number [0:31]
	push %es			# Buffer segment
	push %ax			# Buffer offset (always 0)
	push $1				# Number of sectors to read
	push $16			# Packet size
	mov $0x42, %ah			# Extended read
	mov %sp, %si			# DS:SI -> packet
	int $0x13			# Error code in CF
	popa				# Pop 16 bytes, preserve flags
popa_ret:
	popa
	ret				# Error code still in CF

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
	.fill LOADER_ARG_CNT_LEN, 1, 0

	.org LOADER_ARGS - LOADER_BASE
	.fill LOADER_ARGS_LEN, 1, 0

#### Partition table.
	.org LOADER_PARTS - LOADER_BASE
	.fill LOADER_PARTS_LEN, 1, 0

#### Boot-sector signature for BIOS inspection.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
