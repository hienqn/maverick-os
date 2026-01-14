#!/usr/bin/env bun
/**
 * pintos-mkdisk - Create Pintos virtual disks
 *
 * Usage: pintos-mkdisk [OPTIONS] DISK [-- ARGUMENT...]
 */

import * as fs from "fs";
import { Command } from "commander";
import {
  LOADER_SIZE,
  DEFAULT_GEOMETRY,
  ZIP_GEOMETRY,
  ROLE_ORDER,
  type DiskGeometry,
  type PartitionSource,
  type PartitionRole,
  type AlignmentMode,
  type DiskFormat,
  type PartitionMap,
} from "./lib/types";
import { assembleDisk, readLoader, readPartitionTable, readMbr } from "./lib/disk";

// Global state
const parts: PartitionMap = {};
let format: DiskFormat = "partitioned";
let geometry: DiskGeometry = { ...DEFAULT_GEOMETRY };
let align: AlignmentMode = "bochs";
let loaderFn: string | undefined;
let includeLoader: boolean | undefined;
let kernelArgs: string[] = [];

/**
 * Set a partition source
 */
function setPart(role: PartitionRole, source: "file" | "from" | "size", arg: string): void {
  if (parts[role]) {
    throw new Error(`can't have two sources for ${role.toLowerCase()} partition`);
  }

  const p: PartitionSource = {};

  if (source === "file") {
    // Check if it looks like a partitioned disk
    const mbr = readMbr(arg);
    if (mbr) {
      console.error(`warning: ${arg} looks like a partitioned disk `);
      console.error(`(did you want --${role.toLowerCase()}-from=${arg} or --disk=${arg}?)`);
    }

    p.FILE = arg;
    p.OFFSET = 0;
    p.BYTES = fs.statSync(arg).size;
  } else if (source === "from") {
    const pt = readPartitionTable(arg);
    const sp = pt[role];
    if (!sp) {
      throw new Error(`${arg}: does not contain ${role.toLowerCase()} partition`);
    }

    p.FILE = arg;
    p.OFFSET = sp.START * 512;
    p.BYTES = sp.SECTORS * 512;
  } else if (source === "size") {
    if (!/^\d+(\.\d+)?|\.\d+$/.test(arg)) {
      throw new Error(`${arg}: not a valid size in MB`);
    }

    p.FILE = "/dev/zero";
    p.OFFSET = 0;
    p.BYTES = Math.ceil(parseFloat(arg) * 1024 * 1024);
  }

  parts[role] = p;
}

/**
 * Set disk geometry
 */
function setGeometry(value: string): void {
  if (value === "zip") {
    geometry = { ...ZIP_GEOMETRY };
  } else {
    const match = value.match(/^(\d+)[,\s]+(\d+)$/);
    if (!match) {
      throw new Error("bad syntax for geometry");
    }
    const h = parseInt(match[1], 10);
    const s = parseInt(match[2], 10);
    if (h > 255) throw new Error("heads limited to 255");
    if (s > 63) throw new Error("sectors per track limited to 63");
    geometry = { H: h, S: s };
  }
}

/**
 * Set alignment mode
 */
function setAlign(value: string): void {
  if (value !== "bochs" && value !== "full" && value !== "none") {
    throw new Error(`unknown alignment type "${value}"`);
  }
  align = value;
}

function usage(exitCode: number): never {
  console.log(`pintos-mkdisk, a utility for creating Pintos virtual disks
Usage: pintos-mkdisk [OPTIONS] DISK [-- ARGUMENT...]
where DISK is the virtual disk to create,
      each ARGUMENT is inserted into the command line written to DISK,
  and each OPTION is one of the following options.
Partition options: (where PARTITION is one of: kernel filesys scratch swap)
  --PARTITION=FILE         Use a copy of FILE for the given PARTITION
  --PARTITION-size=SIZE    Create an empty PARTITION of the given SIZE in MB
  --PARTITION-from=DISK    Use of a copy of the given PARTITION in DISK
  (There is no --kernel-size option.)
Output disk options:
  --format=partitioned     Write partition table to output (default)
  --format=raw             Do not write partition table to output
  (Pintos can only use partitioned disks.)
Partitioned format output options:
  --loader[=FILE]          Get bootstrap loader from FILE (default: loader.bin
                           if --kernel option is specified, empty otherwise)
  --no-loader              Do not include a bootstrap loader
  --geometry=H,S           Use H head, S sector geometry (default: 16, 63)
  --geometry=zip           Use 64 head, 32 sector geometry for USB-ZIP boot
                           per http://syslinux.zytor.com/usbkey.php
  --align=bochs            Round size to cylinder for Bochs support (default)
  --align=full             Align partition boundaries to cylinder boundary to
                           let fdisk guess correct geometry and quiet warnings
  --align=none             Don't align partitions at all, to save space
Other options:
  -h, --help               Display this help message.`);
  process.exit(exitCode);
}

function main(): void {
  // Parse arguments manually to handle the -- separator for kernel args
  const args = process.argv.slice(2);
  const dashDashIdx = args.indexOf("--");

  let optArgs: string[];
  if (dashDashIdx !== -1) {
    optArgs = args.slice(0, dashDashIdx);
    kernelArgs = args.slice(dashDashIdx + 1);
  } else {
    optArgs = args;
  }

  // Check for help
  if (optArgs.includes("-h") || optArgs.includes("--help")) {
    usage(0);
  }

  // Parse options manually (Commander doesn't handle our complex option patterns well)
  let diskFn: string | undefined;
  let i = 0;

  while (i < optArgs.length) {
    const arg = optArgs[i];

    if (arg.startsWith("--kernel=")) {
      setPart("KERNEL", "file", arg.substring(9));
    } else if (arg.startsWith("--filesys=")) {
      setPart("FILESYS", "file", arg.substring(10));
    } else if (arg.startsWith("--scratch=")) {
      setPart("SCRATCH", "file", arg.substring(10));
    } else if (arg.startsWith("--swap=")) {
      setPart("SWAP", "file", arg.substring(7));
    } else if (arg.startsWith("--filesys-size=")) {
      setPart("FILESYS", "size", arg.substring(15));
    } else if (arg.startsWith("--scratch-size=")) {
      setPart("SCRATCH", "size", arg.substring(15));
    } else if (arg.startsWith("--swap-size=")) {
      setPart("SWAP", "size", arg.substring(12));
    } else if (arg.startsWith("--kernel-from=")) {
      setPart("KERNEL", "from", arg.substring(14));
    } else if (arg.startsWith("--filesys-from=")) {
      setPart("FILESYS", "from", arg.substring(15));
    } else if (arg.startsWith("--scratch-from=")) {
      setPart("SCRATCH", "from", arg.substring(15));
    } else if (arg.startsWith("--swap-from=")) {
      setPart("SWAP", "from", arg.substring(12));
    } else if (arg.startsWith("--format=")) {
      const fmt = arg.substring(9);
      if (fmt !== "partitioned" && fmt !== "raw") {
        throw new Error(`unknown format: ${fmt}`);
      }
      format = fmt;
    } else if (arg === "--loader") {
      if (includeLoader === false) {
        throw new Error("can't specify both --loader and --no-loader");
      }
      includeLoader = true;
    } else if (arg.startsWith("--loader=")) {
      if (includeLoader === false) {
        throw new Error("can't specify both --loader and --no-loader");
      }
      includeLoader = true;
      loaderFn = arg.substring(9);
    } else if (arg === "--no-loader") {
      if (includeLoader === true) {
        throw new Error("can't specify both --loader and --no-loader");
      }
      includeLoader = false;
    } else if (arg.startsWith("--geometry=")) {
      setGeometry(arg.substring(11));
    } else if (arg.startsWith("--align=")) {
      setAlign(arg.substring(8));
    } else if (arg.startsWith("-")) {
      console.error(`Unknown option: ${arg}`);
      usage(1);
    } else {
      // Positional argument (disk file name)
      if (diskFn) {
        console.error("Too many positional arguments");
        usage(1);
      }
      diskFn = arg;
    }

    i++;
  }

  if (!diskFn) {
    usage(1);
  }

  if (fs.existsSync(diskFn)) {
    throw new Error(`${diskFn}: already exists`);
  }

  // Figure out whether to include a loader
  if (includeLoader === undefined) {
    includeLoader = parts.KERNEL !== undefined && format === "partitioned";
  }

  if (includeLoader && format === "raw") {
    throw new Error("can't write loader to raw disk");
  }

  if (kernelArgs.length > 0 && !includeLoader) {
    throw new Error("can't write command-line arguments without --loader or --kernel");
  }

  if (includeLoader && !parts.KERNEL) {
    console.error(
      "warning: --loader only makes sense without --kernel " +
        "if this disk will be used to load a kernel from another disk"
    );
  }

  // Open disk for writing
  const diskHandle = fs.openSync(diskFn, "w");

  // Read loader if needed
  let loader: Buffer | undefined;
  if (includeLoader) {
    loader = readLoader(loaderFn);
  }

  // Assemble disk
  assembleDisk({
    DISK: diskFn,
    HANDLE: diskHandle,
    ALIGN: align,
    GEOMETRY: geometry,
    FORMAT: format,
    LOADER: loader,
    ARGS: kernelArgs,
    ...parts,
  });

  console.log(`Created ${diskFn}`);
}

try {
  main();
} catch (err) {
  console.error((err as Error).message);
  process.exit(1);
}
