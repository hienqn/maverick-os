#!/usr/bin/env bun
/**
 * pintos-set-cmdline - Modify kernel command line in existing Pintos disk
 *
 * Usage: pintos-set-cmdline DISK -- [ARGUMENT...]
 */

import * as fs from "fs";
import { LOADER_SIZE, MBR_SIGNATURE } from "./lib/types";
import { readFully, writeFully, makeKernelCommandLine } from "./lib/disk";

function usage(exitCode: number): never {
  console.log(`pintos-set-cmdline, a utility for changing the command line in Pintos disks
Usage: pintos-set-cmdline DISK -- [ARGUMENT...]
where DISK is a bootable disk containing a Pintos loader
  and each ARGUMENT is inserted into the command line written to DISK.`);
  process.exit(exitCode);
}

function main(): void {
  const args = process.argv.slice(2);

  // Handle --help
  if (args.length === 1 && args[0] === "--help") {
    usage(0);
  }

  // Parse arguments: DISK -- [ARGUMENT...]
  if (args.length < 2 || args[1] !== "--") {
    usage(1);
  }

  const disk = args[0];
  const kernelArgs = args.slice(2);

  // Open disk for read+write
  const fd = fs.openSync(disk, "r+");

  try {
    // Read MBR (first 512 bytes)
    const mbr = readFully(fd, disk, 512);

    // Check for MBR signature
    const signature = mbr.readUInt16LE(510);
    if (signature !== MBR_SIGNATURE) {
      throw new Error(`${disk}: not a partitioned disk`);
    }

    // Check for Pintos loader signature
    if (!mbr.toString("ascii").includes("Pintos")) {
      throw new Error(`${disk}: does not contain Pintos loader`);
    }

    // Seek to command line location (after loader)
    fs.writeSync(fd, Buffer.alloc(0), 0, 0, LOADER_SIZE);

    // Write the new command line
    const cmdline = makeKernelCommandLine(kernelArgs);

    // Use positioned write
    const written = fs.writeSync(fd, cmdline, 0, cmdline.length, LOADER_SIZE);
    if (written !== cmdline.length) {
      throw new Error(`${disk}: short write`);
    }
  } finally {
    fs.closeSync(fd);
  }
}

main();
