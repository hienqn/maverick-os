#!/usr/bin/env bun
/**
 * backtrace - Convert raw addresses to symbolic backtraces
 *
 * Usage: backtrace [BINARY]... ADDRESS...
 *
 * Wraps addr2line to convert kernel crash addresses into function names
 * and source line numbers.
 */

import * as fs from "fs";
import * as path from "path";

interface Location {
  ADDR: string;
  FUNCTION?: string;
  LINE?: string;
  BINARY?: string;
}

function usage(): never {
  console.log(`backtrace, for converting raw addresses into symbolic backtraces
usage: backtrace [BINARY]... ADDRESS...
where BINARY is the binary file or files from which to obtain symbols
 and ADDRESS is a raw address to convert to a symbol name.

If no BINARY is unspecified, the default is the first of kernel.o or
build/kernel.o that exists.  If multiple binaries are specified, each
symbol printed is from the first binary that contains a match.

The ADDRESS list should be taken from the "Call stack:" printed by the
kernel.  Read "Backtraces" in the "Debugging Tools" chapter of the
Pintos documentation for more information.`);
  process.exit(0);
}

/**
 * Search for an executable in PATH
 */
function searchPath(target: string): string | null {
  const pathEnv = process.env.PATH || "";
  for (const dir of pathEnv.split(":")) {
    const file = path.join(dir, target);
    if (fs.existsSync(file)) {
      return file;
    }
  }
  return null;
}

/**
 * Get the real/canonical path, similar to Perl's realpath
 */
function realPath(p: string): string {
  try {
    return fs.realpathSync(p);
  } catch {
    return p;
  }
}

async function main(): Promise<void> {
  let args = process.argv.slice(2);

  // Handle --help
  if (args.some((a) => a === "-h" || a === "--help")) {
    usage();
  }

  if (args.length === 0) {
    console.error("backtrace: at least one argument required (use --help for help)");
    process.exit(1);
  }

  // Drop garbage inserted by kernel (e.g., "Call stack:" prefix)
  args = args.filter((a) => !/^(call|stack:?|[-+])$/i.test(a));
  args = args.map((a) => a.replace(/\.$/, "")); // Remove trailing dots

  // Separate binaries from addresses
  const binaries: string[] = [];
  while (args.length > 0 && !args[0].startsWith("0x")) {
    const bin = args.shift()!;
    if (!fs.existsSync(bin)) {
      console.error(`backtrace: ${bin}: not found (use --help for help)`);
      process.exit(1);
    }
    binaries.push(bin);
  }

  // Default binary if none specified
  if (binaries.length === 0) {
    if (fs.existsSync("kernel.o")) {
      binaries.push("kernel.o");
    } else if (fs.existsSync("build/kernel.o")) {
      binaries.push("build/kernel.o");
    } else {
      console.error(
        'backtrace: no binary specified and neither "kernel.o" nor "build/kernel.o" exists (use --help for help)'
      );
      process.exit(1);
    }
  }

  // Find addr2line
  const a2l = searchPath("i386-elf-addr2line") || searchPath("addr2line");
  if (!a2l) {
    console.error("backtrace: neither `i386-elf-addr2line' nor `addr2line' in PATH");
    process.exit(1);
  }

  // Build location list
  const locs: Location[] = args.map((addr) => ({ ADDR: addr }));

  // Query each binary
  for (const bin of binaries) {
    const addrs = locs.map((l) => l.ADDR).join(" ");
    const proc = Bun.spawn([a2l, "-fe", bin, ...locs.map((l) => l.ADDR)], {
      stdout: "pipe",
      stderr: "pipe",
    });

    const output = await new Response(proc.stdout).text();
    const lines = output.trim().split("\n");

    // addr2line outputs pairs of lines: function\nfile:line
    for (let i = 0; i < locs.length && i * 2 + 1 < lines.length; i++) {
      const func = lines[i * 2];
      const line = lines[i * 2 + 1];

      // Skip if already resolved from a previous binary
      if (locs[i].BINARY !== undefined) continue;

      // Check if we got a valid result
      if (func !== "??" || line !== "??:0") {
        locs[i].FUNCTION = func;
        locs[i].LINE = line;
        locs[i].BINARY = bin;
      }
    }
  }

  // Print backtrace
  let curBinary: string | undefined;
  for (const loc of locs) {
    // Print binary header if changed and multiple binaries
    if (
      loc.BINARY !== undefined &&
      binaries.length > 1 &&
      (curBinary === undefined || loc.BINARY !== curBinary)
    ) {
      curBinary = loc.BINARY;
      console.log(`In ${curBinary}:`);
    }

    // Format address as 0x00000000
    let addr = loc.ADDR;
    if (/^0x[0-9a-f]+$/i.test(addr)) {
      addr = "0x" + parseInt(addr, 16).toString(16).padStart(8, "0");
    }

    process.stdout.write(`${addr}: `);

    if (loc.BINARY !== undefined) {
      const func = loc.FUNCTION!;
      let line = loc.LINE!;

      // Clean up the line info
      // Remove leading ../
      line = line.replace(/^(\.\.\/)*/, "");

      // Handle absolute paths
      if (line.startsWith("/")) {
        line = realPath(line.split(":")[0]) + ":" + (line.split(":")[1] || "");
        const offset = line.indexOf("pintos/src/");
        if (offset !== -1) {
          line = line.substring(offset);
        }
        // Remove discriminator info
        line = line.replace(/ \(discriminator[^)]*\)/, "");
      }

      console.log(`${func} (${line})`);
    } else {
      console.log("(unknown)");
    }
  }
}

main().catch((err) => {
  console.error(err.message);
  process.exit(1);
});
