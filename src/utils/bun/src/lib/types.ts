/**
 * PintOS utilities - shared type definitions
 */

// Number of bytes available for the loader at the beginning of the MBR.
// Kernel command-line arguments follow the loader.
export const LOADER_SIZE = 314;

// MBR signature
export const MBR_SIGNATURE = 0xaa55;

// Sector size in bytes
export const SECTOR_SIZE = 512;

// PintOS-specific partition types (not standard PC types)
export const PartitionType = {
  KERNEL: 0x20,
  FILESYS: 0x21,
  SCRATCH: 0x22,
  SWAP: 0x23,
} as const;

export type PartitionRole = keyof typeof PartitionType;

// Reverse mapping: type byte -> role name
export const TypeToRole: Record<number, PartitionRole> = {
  0x20: "KERNEL",
  0x21: "FILESYS",
  0x22: "SCRATCH",
  0x23: "SWAP",
};

// Order of roles within a given disk
export const ROLE_ORDER: PartitionRole[] = ["KERNEL", "FILESYS", "SCRATCH", "SWAP"];

/**
 * Disk geometry (CHS - Cylinder/Head/Sector)
 */
export interface DiskGeometry {
  H: number; // Heads (max 255)
  S: number; // Sectors per track (max 63)
}

// Default geometry
export const DEFAULT_GEOMETRY: DiskGeometry = { H: 16, S: 63 };

// Zip disk geometry
export const ZIP_GEOMETRY: DiskGeometry = { H: 64, S: 32 };

/**
 * Partition source - describes where partition data comes from
 */
export interface PartitionSource {
  // Input fields (where to read partition data from)
  FILE?: string; // File to read from (may be "/dev/zero")
  OFFSET?: number; // Byte offset in FILE
  BYTES?: number; // Byte count from FILE

  // Output fields (filled in after disk assembly)
  DISK?: string; // Output disk file name
  START?: number; // Sector offset in DISK
  SECTORS?: number; // Sector count in DISK
}

/**
 * Collection of partitions by role
 */
export type PartitionMap = Partial<Record<PartitionRole, PartitionSource>>;

/**
 * Alignment modes for partition layout
 */
export type AlignmentMode = "bochs" | "full" | "none";

/**
 * Disk format
 */
export type DiskFormat = "partitioned" | "raw";

/**
 * Arguments for disk assembly
 */
export interface AssembleDiskArgs {
  DISK: string; // Output disk file name
  HANDLE: number; // File descriptor for output
  GEOMETRY?: DiskGeometry;
  ALIGN?: AlignmentMode;
  FORMAT?: DiskFormat;
  LOADER?: Buffer; // Bootloader binary
  ARGS?: string[]; // Kernel command-line arguments

  // Partition sources
  KERNEL?: PartitionSource;
  FILESYS?: PartitionSource;
  SCRATCH?: PartitionSource;
  SWAP?: PartitionSource;
}

/**
 * Parsed partition entry from MBR
 */
export interface ParsedPartition {
  START: number; // LBA start sector
  SECTORS: number; // Number of sectors
}

/**
 * Parsed partition table
 */
export type ParsedPartitionTable = Partial<Record<PartitionRole, ParsedPartition>>;
