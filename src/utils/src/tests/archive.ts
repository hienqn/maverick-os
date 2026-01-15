/**
 * Archive/filesystem test verification
 * Port of check_archive and related functions from tests/tests.pm
 */

import { readFileSync, existsSync, statSync, openSync, readSync, closeSync } from "fs";
import {
  readTextFile,
  commonChecks,
  getCoreOutput,
  fail,
  pass,
  log,
} from "./tests";

type FSEntry =
  | { type: "file"; source: string | [string] | [string, number, number] }
  | { type: "directory" };

type FSHierarchy = { [key: string]: FSEntry | FSHierarchyNested };
type FSHierarchyNested = { [key: string]: FSHierarchyNested | FSEntry | string | [string] | [string, number, number] };

interface FlatFS {
  [path: string]: FSEntry;
}

/**
 * Read a ustar-format tar file and return flattened file system
 */
export function readTar(archivePath: string): FlatFS {
  const content: FlatFS = {};

  if (!existsSync(archivePath)) {
    fail(`${archivePath}: file not found`);
  }

  const fd = openSync(archivePath, "r");
  const fileSize = statSync(archivePath).size;
  let offset = 0;

  try {
    while (offset < fileSize) {
      // Read 512-byte header
      const header = Buffer.alloc(512);
      const bytesRead = readSync(fd, header, 0, 512, offset);

      if (bytesRead !== 512) {
        if (bytesRead >= 0) {
          fail(`${archivePath}: unexpected end of file`);
        }
        fail(`${archivePath}: read error`);
      }

      // Check for end of archive (512 zero bytes)
      if (header.every((b) => b === 0)) {
        break;
      }

      // Verify magic numbers (ustar\0 at offset 257, "00" at offset 263)
      const magic = header.slice(257, 263).toString();
      const version = header.slice(263, 265).toString();
      if (magic !== "ustar\0" || version !== "00") {
        fail(`${archivePath}: corrupt ustar header`);
      }

      // Verify checksum
      const storedChecksum = parseInt(
        header.slice(148, 156).toString().replace(/\0/g, "").trim(),
        8
      );
      // Replace checksum field with spaces for calculation
      header.fill(0x20, 148, 156);
      let calcChecksum = 0;
      for (let i = 0; i < 512; i++) {
        calcChecksum += header[i];
      }
      if (storedChecksum !== calcChecksum) {
        fail(`${archivePath}: bad header checksum`);
      }

      // Get file name
      let name = header.slice(0, 100).toString().replace(/\0.*$/, "");
      const prefix = header.slice(345, 500).toString().replace(/\0.*$/, "");
      if (prefix) {
        name = `${prefix}/${name}`;
      }
      if (!name) {
        fail(`${archivePath}: contains file with empty name`);
      }

      // Get type flag
      let typeflag = String.fromCharCode(header[156]);
      if (typeflag === "\0") typeflag = "0";
      if (typeflag !== "0" && typeflag !== "5") {
        fail(`${archivePath}: unknown file type '${typeflag}'`);
      }

      // Get size
      let size = parseInt(
        header.slice(124, 136).toString().replace(/\0/g, "").trim(),
        8
      );
      if (size < 0) {
        fail(`${archivePath}: bad size ${size}`);
      }
      if (typeflag === "5") size = 0;

      // Strip leading "/", "./", "../"
      name = name.replace(/^(\/|\.\/|\.\.\/)*/, "");
      if (name === "." || name === "..") name = "";

      // Store content
      if (content[name] !== undefined) {
        fail(`${archivePath}: contains multiple entries for ${name}`);
      }

      offset += 512; // Move past header

      if (typeflag === "5") {
        // Directory
        if (name) {
          content[name] = { type: "directory" };
        }
      } else {
        // Regular file
        if (!name) {
          fail(`${archivePath}: contains file with empty name`);
        }
        content[name] = {
          type: "file",
          source: [archivePath, offset, size],
        };
        // Skip to next 512-byte boundary
        offset += Math.ceil(size / 512) * 512;
      }
    }
  } finally {
    closeSync(fd);
  }

  return content;
}

/**
 * Flatten a hierarchical file system structure
 */
export function flattenHierarchy(hier: FSHierarchyNested, prefix: string = ""): FlatFS {
  const flat: FlatFS = {};

  for (const name of Object.keys(hier)) {
    const value = hier[name];

    if (typeof value === "object" && !Array.isArray(value) && !("type" in value)) {
      // Nested directory
      Object.assign(flat, flattenHierarchy(value as FSHierarchyNested, `${prefix}${name}/`));
      flat[`${prefix}${name}`] = { type: "directory" };
    } else if (typeof value === "string") {
      // File path reference
      flat[`${prefix}${name}`] = { type: "file", source: value };
    } else if (Array.isArray(value)) {
      // Literal content or [file, offset, length]
      flat[`${prefix}${name}`] = { type: "file", source: value as [string] | [string, number, number] };
    } else {
      flat[`${prefix}${name}`] = value as FSEntry;
    }
  }

  return flat;
}

/**
 * Normalize file system by resolving file references to [file, offset, length]
 */
export function normalizeFS(fs: FlatFS): FlatFS {
  const result: FlatFS = {};

  for (const name of Object.keys(fs)) {
    const entry = fs[name];
    if (entry.type === "directory") {
      result[name] = entry;
    } else if (typeof entry.source === "string") {
      // File path - resolve to [path, 0, size]
      if (!existsSync(entry.source)) {
        fail(`can't open ${entry.source}`);
      }
      const size = statSync(entry.source).size;
      result[name] = { type: "file", source: [entry.source, 0, size] };
    } else {
      result[name] = entry;
    }
  }

  return result;
}

/**
 * Get file size from entry
 */
function fileSize(entry: FSEntry): number {
  if (entry.type === "directory") {
    throw new Error("Cannot get size of directory");
  }
  const source = entry.source;
  if (Array.isArray(source)) {
    if (source.length === 1) {
      return source[0].length; // Literal content
    } else {
      return source[2]; // [file, offset, length]
    }
  }
  return statSync(source).size;
}

/**
 * Compare two files byte by byte
 */
function compareFiles(
  aSource: [string] | [string, number, number],
  bSource: [string] | [string, number, number],
  name: string,
  verbose: boolean
): boolean {
  let aData: Buffer;
  let bData: Buffer;

  // Read file A
  if (aSource.length === 1) {
    aData = Buffer.from(aSource[0]);
  } else {
    const [path, offset, length] = aSource;
    aData = Buffer.alloc(length);
    const fd = openSync(path, "r");
    readSync(fd, aData, 0, length, offset);
    closeSync(fd);
  }

  // Read file B
  if (bSource.length === 1) {
    bData = Buffer.from(bSource[0]);
  } else {
    const [path, offset, length] = bSource;
    bData = Buffer.alloc(length);
    const fd = openSync(path, "r");
    readSync(fd, bData, 0, length, offset);
    closeSync(fd);
  }

  // Compare
  if (aData.equals(bData)) {
    return true;
  }

  // Find first difference
  const minLen = Math.min(aData.length, bData.length);
  let diffOfs = 0;
  for (diffOfs = 0; diffOfs < minLen; diffOfs++) {
    if (aData[diffOfs] !== bData[diffOfs]) break;
  }

  log(`\nFile ${name} differs from expected starting at offset 0x${diffOfs.toString(16)}.`);

  if (verbose) {
    log("Expected contents:");
    hexDump(aData.slice(diffOfs, diffOfs + 64), diffOfs);
    log("Actual contents:");
    hexDump(bData.slice(diffOfs, diffOfs + 64), diffOfs);
  }

  return false;
}

/**
 * Hex dump for debugging
 */
function hexDump(data: Buffer, startOfs: number): void {
  if (data.length === 0) {
    log(`  (File ends at offset ${startOfs.toString(16).padStart(8, "0")}.)`);
    return;
  }

  const perLine = 16;
  let ofs = startOfs;
  let pos = 0;

  while (pos < data.length) {
    const lineStart = ofs % perLine;
    const lineEnd = Math.min(perLine, lineStart + (data.length - pos));
    const n = lineEnd - lineStart;

    let line = `0x${(Math.floor(ofs / perLine) * perLine).toString(16).padStart(8, "0")}  `;

    // Hex part
    line += "   ".repeat(lineStart);
    for (let i = 0; i < n; i++) {
      line += data[pos + i].toString(16).padStart(2, "0");
      line += i === perLine / 2 - 1 ? "-" : " ";
    }
    line += "   ".repeat(perLine - lineEnd);

    // Character part
    line += "|" + " ".repeat(lineStart);
    for (let i = 0; i < n; i++) {
      const ch = data[pos + i];
      line += ch >= 0x20 && ch < 0x7f ? String.fromCharCode(ch) : ".";
    }
    line += " ".repeat(perLine - lineEnd) + "|";

    log(line);

    pos += n;
    ofs += n;
  }
}

/**
 * Print file system contents
 */
function printFS(fs: FlatFS): void {
  const names = Object.keys(fs).sort();
  if (names.length === 0) {
    log("(empty)");
    return;
  }

  for (const name of names) {
    const entry = fs[name];
    const escName = name.replace(/[^\x20-\x7e]/g, ".");
    if (entry.type === "directory") {
      log(`${escName}: directory`);
    } else {
      log(`${escName}: ${fileSize(entry)}-byte file`);
    }
  }
}

/**
 * Check extracted archive against expected contents
 */
export function checkArchive(
  testName: string,
  expectedHier: FSHierarchyNested,
  prereqTests: string[]
): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("file system extraction run", output);

  const coreOutput = getCoreOutput("file system extraction run", output);
  // Filter out exit codes
  const filteredOutput = coreOutput.filter(
    (line) => !/^[a-zA-Z0-9-_]+: exit\(\d+\)$/.test(line)
  );

  if (filteredOutput.length > 0) {
    fail(`Error extracting file system:\n${filteredOutput.join("\n")}`);
  }

  // Add test binary and tar utility to expected hierarchy
  const testBaseName = testName
    .replace(/.*\//, "")
    .replace(/-persistence$/, "");
  expectedHier[testBaseName] = prereqTests[0];
  expectedHier["tar"] = "tests/filesys/extended/tar";

  // Flatten and normalize
  const expected = normalizeFS(flattenHierarchy(expectedHier, ""));
  const actual = readTar(`${prereqTests[0]}.tar`);

  let errors = 0;

  // Check expected files exist
  for (const name of Object.keys(expected).sort()) {
    if (actual[name] !== undefined) {
      const actEntry = actual[name];
      const expEntry = expected[name];
      if (actEntry.type === "directory" && expEntry.type !== "directory") {
        log(`${name} is a directory but should be an ordinary file.`);
        errors++;
      } else if (actEntry.type !== "directory" && expEntry.type === "directory") {
        log(`${name} is an ordinary file but should be a directory.`);
        errors++;
      }
    } else {
      log(`${name} is missing from the file system.`);
      errors++;
    }
  }

  // Check for unexpected files
  for (const name of Object.keys(actual).sort()) {
    if (expected[name] === undefined) {
      const escName = name.replace(/[^\x20-\x7e]/g, ".");
      if (escName === name) {
        log(`${name} exists in the file system but it should not.`);
      } else {
        log(
          `${escName} exists in the file system but should not. (The name ` +
            `of this file contains unusual characters that were printed as '.')`
        );
      }
      errors++;
    }
  }

  if (errors > 0) {
    log("\nActual contents of file system:");
    printFS(actual);
    log("\nExpected contents of file system:");
    printFS(expected);
  } else {
    // Compare file contents
    for (const name of Object.keys(expected).sort()) {
      const expEntry = expected[name];
      const actEntry = actual[name];
      if (expEntry.type !== "directory" && actEntry.type !== "directory") {
        const expSource = expEntry.source as [string] | [string, number, number];
        const actSource = actEntry.source as [string] | [string, number, number];
        if (!compareFiles(expSource, actSource, name, errors === 0)) {
          errors++;
        }
      }
    }
  }

  if (errors > 0) {
    fail("Extracted file system contents are not correct.");
  }

  pass();
}
