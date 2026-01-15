/**
 * PintOS utilities - disk operations
 *
 * Handles MBR creation/parsing, partition tables, and disk geometry.
 */

import * as fs from "fs";
import {
  LOADER_SIZE,
  MBR_SIGNATURE,
  SECTOR_SIZE,
  PartitionType,
  TypeToRole,
  ROLE_ORDER,
  DEFAULT_GEOMETRY,
  type DiskGeometry,
  type PartitionSource,
  type PartitionRole,
  type AlignmentMode,
  type DiskFormat,
  type AssembleDiskArgs,
  type ParsedPartition,
  type ParsedPartitionTable,
} from "./types";

// ============================================================================
// Math utilities
// ============================================================================

/**
 * Divide and round up to nearest integer
 */
export function divRoundUp(x: number, y: number): number {
  return Math.floor((Math.ceil(x) + y - 1) / y);
}

/**
 * Round x up to the nearest multiple of y
 */
export function roundUp(x: number, y: number): number {
  return divRoundUp(x, y) * y;
}

/**
 * Number of sectors in a cylinder
 */
export function cylSectors(geometry: DiskGeometry): number {
  return geometry.H * geometry.S;
}

// ============================================================================
// CHS geometry conversions
// ============================================================================

/**
 * Convert LBA (Logical Block Address) to CHS (Cylinder/Head/Sector)
 */
export function lbaToChs(
  lba: number,
  geometry: DiskGeometry
): { cyl: number; head: number; sect: number } {
  const hpc = geometry.H; // Heads per cylinder
  const spt = geometry.S; // Sectors per track

  const cyl = Math.floor(lba / (hpc * spt));
  const temp = lba % (hpc * spt);
  const head = Math.floor(temp / spt);
  const sect = (temp % spt) + 1; // Sectors are 1-indexed in CHS

  // CHS has limits: cylinder max 1023, head max 254, sector max 63
  if (cyl <= 1023) {
    return { cyl, head, sect };
  } else {
    return { cyl: 1023, head: 254, sect: 63 };
  }
}

/**
 * Pack CHS into 3-byte format used in MBR partition tables
 *
 * Format (per PC partition table spec):
 * - Byte 0: Head
 * - Byte 1: Sector (bits 0-5) | Cylinder high bits (bits 6-7)
 * - Byte 2: Cylinder low 8 bits
 */
export function packChs(lba: number, geometry: DiskGeometry): Buffer {
  const { cyl, head, sect } = lbaToChs(lba, geometry);
  const buf = Buffer.alloc(3);
  buf[0] = head;
  buf[1] = sect | ((cyl >> 2) & 0xc0);
  buf[2] = cyl & 0xff;
  return buf;
}

// ============================================================================
// Partition table operations
// ============================================================================

/**
 * Create a 64-byte MBR partition table
 */
export function makePartitionTable(
  geometry: DiskGeometry,
  partitions: Partial<Record<PartitionRole, PartitionSource>>
): Buffer {
  const table = Buffer.alloc(64);
  let offset = 0;

  for (const role of ROLE_ORDER) {
    const p = partitions[role];
    if (!p || p.START === undefined || p.SECTORS === undefined) continue;

    const end = p.START + p.SECTORS - 1;
    const bootable = role === "KERNEL";

    // Each partition entry is 16 bytes:
    // - 1 byte: bootable flag (0x80 = bootable, 0x00 = not)
    // - 3 bytes: CHS of partition start
    // - 1 byte: partition type
    // - 3 bytes: CHS of partition end
    // - 4 bytes: LBA of partition start (little-endian)
    // - 4 bytes: partition length in sectors (little-endian)

    table[offset] = bootable ? 0x80 : 0x00;
    packChs(p.START, geometry).copy(table, offset + 1);
    table[offset + 4] = PartitionType[role];
    packChs(end, geometry).copy(table, offset + 5);
    table.writeUInt32LE(p.START, offset + 8);
    table.writeUInt32LE(p.SECTORS, offset + 12);

    offset += 16;
  }

  return table;
}

/**
 * Create kernel command-line bytes for MBR
 *
 * Format:
 * - 4 bytes: argument count (little-endian)
 * - 128 bytes: null-terminated argument strings
 */
export function makeKernelCommandLine(args: string[]): Buffer {
  const argsStr = args.map((a) => a + "\0").join("");
  if (argsStr.length > 128) {
    throw new Error("command line exceeds 128 bytes");
  }

  const buf = Buffer.alloc(132);
  buf.writeUInt32LE(args.length, 0);
  Buffer.from(argsStr, "utf8").copy(buf, 4);
  return buf;
}

// ============================================================================
// MBR reading/parsing
// ============================================================================

/**
 * Read MBR from a file. Returns the 512-byte MBR if valid, null otherwise.
 */
export function readMbr(file: string): Buffer | null {
  const stats = fs.statSync(file);
  if (stats.size === 0) {
    throw new Error(`${file}: file has zero size`);
  }
  if (stats.size < 512) {
    return null;
  }

  const fd = fs.openSync(file, "r");
  const mbr = Buffer.alloc(512);
  fs.readSync(fd, mbr, 0, 512, 0);
  fs.closeSync(fd);

  // Check for MBR signature (0xAA55 at offset 510)
  const signature = mbr.readUInt16LE(510);
  return signature === MBR_SIGNATURE ? mbr : null;
}

/**
 * Parse partition table from MBR
 */
export function interpretPartitionTable(mbr: Buffer, disk: string): ParsedPartitionTable {
  const parts: ParsedPartitionTable = {};

  for (let i = 0; i < 4; i++) {
    const entryOffset = 446 + 16 * i;
    const entry = mbr.subarray(entryOffset, entryOffset + 16);

    const bootable = entry[0];
    const type = entry[4];
    const lbaStart = entry.readUInt32LE(8);
    const lbaLength = entry.readUInt32LE(12);

    // Skip empty entries
    if (type === 0) continue;

    // Validate bootable flag
    if (bootable !== 0x00 && bootable !== 0x80) {
      console.error(`warning: invalid partition entry ${i} in ${disk}`);
      continue;
    }

    // Check for PintOS partition type
    const role = TypeToRole[type];
    if (!role) {
      console.error(
        `warning: non-Pintos partition type 0x${type.toString(16).padStart(2, "0")} in ${disk}`
      );
      continue;
    }

    // Check for duplicates
    if (parts[role]) {
      console.error(`warning: duplicate ${role.toLowerCase()} partition in ${disk}`);
      continue;
    }

    parts[role] = {
      START: lbaStart,
      SECTORS: lbaLength,
    };
  }

  return parts;
}

/**
 * Read and parse partition table from a disk file
 */
export function readPartitionTable(file: string): ParsedPartitionTable {
  const mbr = readMbr(file);
  if (!mbr) {
    throw new Error(`${file}: not a partitioned disk`);
  }
  return interpretPartitionTable(mbr, file);
}

// ============================================================================
// File I/O utilities
// ============================================================================

/**
 * Read exactly n bytes from a file descriptor
 */
export function readFully(fd: number, fileName: string, bytes: number): Buffer {
  const data = Buffer.alloc(bytes);
  const bytesRead = fs.readSync(fd, data, 0, bytes, null);
  if (bytesRead !== bytes) {
    throw new Error(`${fileName}: unexpected end of file`);
  }
  return data;
}

/**
 * Write data to a file descriptor
 */
export function writeFully(fd: number, fileName: string, data: Buffer): void {
  const written = fs.writeSync(fd, data);
  if (written !== data.length) {
    throw new Error(`${fileName}: short write`);
  }
}

/**
 * Write n zero bytes to a file descriptor
 */
export function writeZeros(fd: number, fileName: string, size: number): void {
  const chunkSize = 4096;
  const zeroChunk = Buffer.alloc(chunkSize);

  while (size > 0) {
    const toWrite = Math.min(chunkSize, size);
    writeFully(fd, fileName, zeroChunk.subarray(0, toWrite));
    size -= toWrite;
  }
}

/**
 * Copy bytes between file descriptors
 */
export function copyFile(
  fromFd: number,
  fromName: string,
  toFd: number,
  toName: string,
  size: number
): void {
  const chunkSize = 4096;

  while (size > 0) {
    const toRead = Math.min(chunkSize, size);
    const data = readFully(fromFd, fromName, toRead);
    writeFully(toFd, toName, data);
    size -= toRead;
  }
}

// ============================================================================
// Disk assembly
// ============================================================================

/**
 * Assemble a virtual disk from partitions
 *
 * This is the main disk creation function, equivalent to Perl's assemble_disk().
 */
export function assembleDisk(args: AssembleDiskArgs): void {
  const geometry = args.GEOMETRY || DEFAULT_GEOMETRY;
  const format: DiskFormat = args.FORMAT || "partitioned";

  // Determine alignment mode
  let align: boolean;
  let pad: boolean;
  const alignMode: AlignmentMode = args.ALIGN || "bochs";

  if (alignMode === "bochs") {
    align = false;
    pad = true;
  } else if (alignMode === "full") {
    align = true;
    pad = false;
  } else {
    align = false;
    pad = false;
  }

  // Count partitions
  const partCount = ROLE_ORDER.filter((role) => args[role] !== undefined).length;

  if (format === "raw" && partCount !== 1) {
    throw new Error("must have exactly one partition for raw output");
  }

  // Calculate disk layout
  let totalSectors = 0;
  if (format === "partitioned") {
    totalSectors += align ? geometry.S : 1;
  }

  for (const role of ROLE_ORDER) {
    const p = args[role];
    if (!p) continue;

    if (p.DISK) {
      throw new Error(`partition ${role} already has DISK set`);
    }

    const bytes = p.BYTES || 0;
    const start = totalSectors;
    let end = start + divRoundUp(bytes, SECTOR_SIZE);
    if (align) {
      end = roundUp(end, cylSectors(geometry));
    }

    p.DISK = args.DISK;
    p.START = start;
    p.SECTORS = end - start;
    totalSectors = end;
  }

  // Write the disk
  const diskFd = args.HANDLE;
  const diskFn = args.DISK;

  if (format === "partitioned") {
    // Build MBR
    const loader = args.LOADER || Buffer.from([0xcd, 0x18]); // Default: INT 0x18 (boot failure)
    const mbr = Buffer.alloc(512);

    // Copy loader (padded to LOADER_SIZE)
    const loaderPadded = Buffer.alloc(LOADER_SIZE);
    loader.copy(loaderPadded);
    loaderPadded.copy(mbr, 0);

    // Add kernel command line
    const cmdline = makeKernelCommandLine(args.ARGS || []);
    cmdline.copy(mbr, LOADER_SIZE);

    // Add partition table
    const partTable = makePartitionTable(geometry, args);
    partTable.copy(mbr, 446);

    // Add MBR signature
    mbr.writeUInt16LE(MBR_SIGNATURE, 510);

    writeFully(diskFd, diskFn, mbr);

    // Pad to end of first cylinder if aligning
    if (align) {
      writeZeros(diskFd, diskFn, SECTOR_SIZE * (geometry.S - 1));
    }
  }

  // Write partition contents
  for (const role of ROLE_ORDER) {
    const p = args[role];
    if (!p || !p.FILE || p.BYTES === undefined) continue;

    const sourceFd = fs.openSync(p.FILE, "r");
    if (p.OFFSET) {
      fs.readSync(sourceFd, Buffer.alloc(0), 0, 0, p.OFFSET); // Seek
      const seekPos = fs.lstatSync(p.FILE).size >= p.OFFSET ? p.OFFSET : 0;
      // Use pread-style positioning
    }

    // For /dev/zero, just write zeros
    if (p.FILE === "/dev/zero") {
      writeZeros(diskFd, diskFn, p.BYTES);
    } else {
      // Seek to offset
      const data = Buffer.alloc(p.BYTES);
      fs.readSync(sourceFd, data, 0, p.BYTES, p.OFFSET || 0);
      writeFully(diskFd, diskFn, data);
    }

    fs.closeSync(sourceFd);

    // Pad partition to full size
    const paddingBytes = (p.SECTORS || 0) * SECTOR_SIZE - p.BYTES;
    if (paddingBytes > 0) {
      writeZeros(diskFd, diskFn, paddingBytes);
    }
  }

  // Pad to cylinder boundary if needed
  if (pad) {
    const padSectors = roundUp(totalSectors, cylSectors(geometry));
    const paddingBytes = (padSectors - totalSectors) * SECTOR_SIZE;
    if (paddingBytes > 0) {
      writeZeros(diskFd, diskFn, paddingBytes);
    }
  }

  fs.closeSync(diskFd);
}

// ============================================================================
// Loader utilities
// ============================================================================

/**
 * Find a file in common locations
 */
export function findFile(baseName: string): string | null {
  const candidates = [baseName, `build/${baseName}`];
  for (const path of candidates) {
    if (fs.existsSync(path)) {
      return path;
    }
  }
  return null;
}

/**
 * Read the bootloader binary
 */
export function readLoader(fileName?: string): Buffer {
  const name = fileName || findFile("loader.bin");
  if (!name) {
    throw new Error("Cannot find loader");
  }

  const stats = fs.statSync(name);
  if (stats.size !== LOADER_SIZE && stats.size !== 512) {
    throw new Error(`${name}: must be exactly ${LOADER_SIZE} or 512 bytes long`);
  }

  const fd = fs.openSync(name, "r");
  const loader = readFully(fd, name, LOADER_SIZE);
  fs.closeSync(fd);
  return loader;
}
