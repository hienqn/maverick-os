/**
 * PintOS utilities - ustar archive format
 *
 * Handles reading/writing files in ustar (Unix Standard TAR) format
 * for file transfer to/from the virtual machine via scratch disk.
 */

import * as fs from "fs";
import { readFully, writeFully, copyFile } from "./disk";

/**
 * ustar header layout (512 bytes total):
 * - 0-99: file name (100 bytes)
 * - 100-107: mode (8 bytes, octal)
 * - 108-115: uid (8 bytes, octal)
 * - 116-123: gid (8 bytes, octal)
 * - 124-135: size (12 bytes, octal)
 * - 136-147: mtime (12 bytes, octal)
 * - 148-155: checksum (8 bytes, octal)
 * - 156: typeflag (1 byte)
 * - 157-256: linkname (100 bytes)
 * - 257-262: magic "ustar\0" (6 bytes)
 * - 263-264: version "00" (2 bytes)
 * - 265-296: uname (32 bytes)
 * - 297-328: gname (32 bytes)
 * - 329-336: devmajor (8 bytes)
 * - 337-344: devminor (8 bytes)
 * - 345-499: prefix (155 bytes)
 * - 500-511: padding (12 bytes)
 */

const USTAR_MAGIC = "ustar\0";
const USTAR_VERSION = "00";

// Fixed mtime (Jan 1, 2006 - matches original Perl code)
const FIXED_MTIME = 1136102400;

/**
 * Format a number as an octal string for ustar header field
 */
export function mkUstarField(number: number, size: number): Buffer {
  const len = size - 1;
  const octal = number.toString(8).padStart(len, "0") + "\0";

  if (octal.length !== size) {
    throw new Error(`${number}: too large for ${size}-byte octal ustar field`);
  }

  return Buffer.from(octal, "ascii");
}

/**
 * Calculate ustar header checksum
 *
 * The checksum is computed by treating the checksum field (bytes 148-155)
 * as spaces, then summing all bytes in the header.
 */
export function calcUstarChecksum(header: Buffer): number {
  if (header.length !== 512) {
    throw new Error("header must be 512 bytes");
  }

  // Create a copy with checksum field replaced by spaces
  const copy = Buffer.from(header);
  for (let i = 148; i < 156; i++) {
    copy[i] = 0x20; // space
  }

  // Sum all bytes
  let sum = 0;
  for (let i = 0; i < 512; i++) {
    sum += copy[i];
  }

  return sum;
}

/**
 * Put a file onto the scratch disk in ustar format
 *
 * @param srcFileName - Source file path on host
 * @param dstFileName - Destination file name in archive (max 99 chars)
 * @param diskFd - File descriptor for scratch disk
 * @param diskFileName - Disk file name (for error messages)
 */
export function putScratchFile(
  srcFileName: string,
  dstFileName: string,
  diskFd: number,
  diskFileName: string
): void {
  console.log(`Copying ${srcFileName} to scratch partition...`);

  // ustar format supports up to 100 characters, but PintOS kernel only supports 99
  if (dstFileName.length > 99) {
    throw new Error(`${dstFileName}: name too long (max 99 characters)`);
  }

  // Get file size
  const stats = fs.statSync(srcFileName);
  const size = stats.size;

  // Compose ustar header
  const header = Buffer.alloc(512);
  let offset = 0;

  // name (100 bytes)
  Buffer.from(dstFileName, "utf8").copy(header, offset);
  offset += 100;

  // mode (8 bytes) - 0644
  mkUstarField(0o644, 8).copy(header, offset);
  offset += 8;

  // uid (8 bytes) - 0
  mkUstarField(0, 8).copy(header, offset);
  offset += 8;

  // gid (8 bytes) - 0
  mkUstarField(0, 8).copy(header, offset);
  offset += 8;

  // size (12 bytes)
  mkUstarField(size, 12).copy(header, offset);
  offset += 12;

  // mtime (12 bytes)
  mkUstarField(FIXED_MTIME, 12).copy(header, offset);
  offset += 12;

  // checksum placeholder (8 bytes) - filled with spaces
  header.fill(0x20, offset, offset + 8);
  offset += 8;

  // typeflag (1 byte) - '0' for regular file
  header[offset] = 0x30; // ASCII '0'
  offset += 1;

  // linkname (100 bytes) - empty
  offset += 100;

  // magic (6 bytes) - "ustar\0"
  Buffer.from(USTAR_MAGIC, "ascii").copy(header, offset);
  offset += 6;

  // version (2 bytes) - "00"
  Buffer.from(USTAR_VERSION, "ascii").copy(header, offset);
  offset += 2;

  // uname (32 bytes) - "root"
  Buffer.from("root", "utf8").copy(header, offset);
  offset += 32;

  // gname (32 bytes) - "root"
  Buffer.from("root", "utf8").copy(header, offset);
  offset += 32;

  // devmajor (8 bytes) - empty
  offset += 8;

  // devminor (8 bytes) - empty
  offset += 8;

  // prefix (155 bytes) - empty
  offset += 155;

  // padding (12 bytes) - already zeros
  // Total: 512 bytes

  // Calculate and set checksum
  const checksum = calcUstarChecksum(header);
  mkUstarField(checksum, 8).copy(header, 148);

  // Write header
  writeFully(diskFd, diskFileName, header);

  // Copy file data
  const srcFd = fs.openSync(srcFileName, "r");
  copyFile(srcFd, srcFileName, diskFd, diskFileName, size);

  // Verify size didn't change
  const newStats = fs.fstatSync(srcFd);
  if (newStats.size !== size) {
    fs.closeSync(srcFd);
    throw new Error(`${srcFileName}: changed size while being read`);
  }
  fs.closeSync(srcFd);

  // Pad to sector boundary
  if (size % 512 !== 0) {
    const padding = Buffer.alloc(512 - (size % 512));
    writeFully(diskFd, diskFileName, padding);
  }
}

/**
 * Get a file from the scratch disk in ustar format
 *
 * @param getFileName - Destination file path on host
 * @param diskFd - File descriptor for scratch disk
 * @param diskFileName - Disk file name (for error messages)
 * @returns null on success, error message string on failure
 */
export function getScratchFile(
  getFileName: string,
  diskFd: number,
  diskFileName: string
): string | null {
  console.log(`Copying ${getFileName} out of ${diskFileName}...`);

  // Read ustar header
  const header = readFully(diskFd, diskFileName, 512);

  // Check for end of archive (all zeros)
  if (header.every((b) => b === 0)) {
    return "scratch disk tar archive ends unexpectedly";
  }

  // Verify magic
  const magic = header.subarray(257, 263).toString("ascii");
  if (magic !== USTAR_MAGIC) {
    return "corrupt ustar signature";
  }

  // Verify version
  const version = header.subarray(263, 265).toString("ascii");
  if (version !== USTAR_VERSION) {
    return "invalid ustar version";
  }

  // Verify checksum
  const checksumStr = header.subarray(148, 156).toString("ascii").replace(/\0.*$/, "");
  const checksum = parseInt(checksumStr, 8);
  const correctChecksum = calcUstarChecksum(header);
  if (checksum !== correctChecksum) {
    return "checksum mismatch";
  }

  // Get type
  const typeflag = String.fromCharCode(header[156]);
  if (typeflag !== "0" && typeflag !== "\0") {
    return "not a regular file";
  }

  // Get size
  const sizeStr = header.subarray(124, 136).toString("ascii").replace(/\0.*$/, "");
  const size = parseInt(sizeStr, 8);
  if (size < 0) {
    return `bad size ${size}`;
  }

  // Copy file data
  const getFd = fs.openSync(getFileName, "w", 0o666);
  copyFile(diskFd, diskFileName, getFd, getFileName, size);
  fs.closeSync(getFd);

  // Skip to next sector boundary
  if (size % 512 !== 0) {
    readFully(diskFd, diskFileName, 512 - (size % 512));
  }

  return null; // Success
}

/**
 * Extract file name from ustar header
 */
export function getUstarFileName(header: Buffer): string {
  // Name is in first 100 bytes, null-terminated
  const nameBytes = header.subarray(0, 100);
  const nullPos = nameBytes.indexOf(0);
  return nameBytes.subarray(0, nullPos === -1 ? 100 : nullPos).toString("utf8");
}

/**
 * Extract file size from ustar header
 */
export function getUstarFileSize(header: Buffer): number {
  const sizeStr = header.subarray(124, 136).toString("ascii").replace(/\0.*$/, "");
  return parseInt(sizeStr, 8);
}
