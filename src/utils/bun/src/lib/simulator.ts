/**
 * PintOS utilities - simulator launching
 *
 * Handles running Bochs, QEMU, and VMware Player.
 */

import * as fs from "fs";
import * as path from "path";
import { xsystem, runCommand, findInPath, type XSystemOptions } from "./subprocess";

export type Simulator = "bochs" | "qemu" | "player";
export type Debugger = "none" | "monitor" | "gdb";
export type VgaMode = "window" | "terminal" | "none";

export interface SimulatorOptions {
  sim: Simulator;
  debug: Debugger;
  mem: number; // Memory in MB
  serial: boolean;
  vga: VgaMode;
  jitter?: number;
  realtime?: boolean;
  timeout?: number;
  killOnFailure?: boolean;
  disks: (string | undefined)[];
}

/**
 * Get disk geometry for an existing disk file
 */
export function diskGeometry(file: string): {
  CAPACITY: number;
  C: number;
  H: number;
  S: number;
} {
  const stats = fs.statSync(file);
  const size = stats.size;

  if (size % 512 !== 0) {
    throw new Error(`${file}: size ${size} not a multiple of 512 bytes`);
  }

  const cylSize = 512 * 16 * 63;
  const cylinders = Math.ceil(size / cylSize);

  return {
    CAPACITY: size / 512,
    C: cylinders,
    H: 16,
    S: 63,
  };
}

/**
 * Run the selected simulator
 */
export async function runVm(options: SimulatorOptions): Promise<void> {
  switch (options.sim) {
    case "bochs":
      await runBochs(options);
      break;
    case "qemu":
      await runQemu(options);
      break;
    case "player":
      await runPlayer(options);
      break;
    default:
      throw new Error(`unknown simulator '${options.sim}'`);
  }
}

/**
 * Run Bochs simulator
 */
async function runBochs(options: SimulatorOptions): Promise<void> {
  const { debug, mem, serial, vga, jitter, realtime, timeout, killOnFailure, disks } = options;

  // Select Bochs binary
  const bin = debug === "monitor" ? "bochs-dbg" : "bochs";

  // Check for squish-pty
  let squishPty: string | null = null;
  if (serial) {
    squishPty = findInPath("squish-pty");
    if (!squishPty) {
      console.log("warning: can't find squish-pty, so terminal input will fail");
    }
  }

  // Write bochsrc.txt
  let config = `romimage: file=$BXSHARE/BIOS-bochs-latest
vgaromimage: file=$BXSHARE/VGABIOS-lgpl-latest
boot: disk
cpu: ips=1000000
megs: ${mem}
log: bochsout.txt
panic: action=fatal
user_shortcut: keys=ctrlaltdel
`;

  if (debug === "gdb") {
    config += "gdbstub: enabled=1\n";
  }

  config += `clock: sync=${realtime ? "realtime" : "none"}, time0=0\n`;

  if (disks.filter((d) => d !== undefined).length > 2) {
    config += "ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n";
  }

  // Add disk configurations
  const diskNames = ["ata0-master", "ata0-slave", "ata1-master", "ata1-slave"];
  for (let i = 0; i < 4; i++) {
    const disk = disks[i];
    if (disk) {
      const geom = diskGeometry(disk);
      config += `${diskNames[i]}: type=disk, path=${disk}, mode=flat, `;
      config += `cylinders=${geom.C}, heads=${geom.H}, spt=${geom.S}, `;
      config += "translation=none\n";
    }
  }

  // Serial and display settings
  if (vga !== "terminal") {
    if (serial) {
      const mode = squishPty ? "term" : "file";
      config += `com1: enabled=1, mode=${mode}, dev=/dev/stdout\n`;
    }
    if (vga === "none") {
      config += "display_library: nogui\n";
    }
  } else {
    config += "display_library: term\n";
  }

  fs.writeFileSync("bochsrc.txt", config);

  // Build command line
  const cmd: string[] = [];
  if (squishPty) cmd.push(squishPty);
  cmd.push(bin, "-q");
  if (jitter !== undefined) cmd.push("-j", String(jitter));

  // Run Bochs
  console.log(cmd.join(" "));
  const exitCode = await xsystem(cmd, { timeout, killOnFailure });

  // Bochs normally exits with 1, which is weird
  if (exitCode !== 0 && exitCode !== 1) {
    throw new Error(`Bochs died: code ${exitCode}`);
  }
}

/**
 * Run QEMU simulator
 */
async function runQemu(options: SimulatorOptions): Promise<void> {
  const { debug, mem, serial, vga, jitter, timeout, killOnFailure, disks } = options;

  if (vga === "terminal") {
    console.log("warning: qemu doesn't support --terminal");
  }
  if (jitter !== undefined) {
    console.log("warning: qemu doesn't support jitter");
  }

  const cmd: string[] = ["qemu-system-i386"];
  cmd.push("-device", "isa-debug-exit");

  // Add disks
  const hdFlags = ["-hda", "-hdb", "-hdc", "-hdd"];
  for (let i = 0; i < 4; i++) {
    if (disks[i]) {
      cmd.push(hdFlags[i], disks[i]!);
    }
  }

  cmd.push("-m", String(mem));
  cmd.push("-netdev", "user,id=net0");
  cmd.push("-device", "e1000,netdev=net0");

  if (vga === "none") {
    cmd.push("-nographic");
  }
  if (serial && vga !== "none") {
    cmd.push("-serial", "stdio");
  }
  if (debug === "monitor") {
    cmd.push("-S");
  }
  if (debug === "gdb") {
    cmd.push("-s", "-S");
  }
  if (vga === "none" && debug === "none") {
    cmd.push("-monitor", "null");
  }

  await runCommand(cmd, { timeout, killOnFailure });
}

/**
 * Run VMware Player
 */
async function runPlayer(options: SimulatorOptions): Promise<void> {
  const { debug, mem: rawMem, serial, vga, jitter, timeout, killOnFailure, disks } = options;

  // Warn about unsupported features
  if (debug !== "none") console.log(`warning: no support for --${debug} with VMware Player`);
  if (vga === "none") console.log("warning: no support for --no-vga with VMware Player");
  if (vga === "terminal") console.log("warning: no support for --terminal with VMware Player");
  if (jitter !== undefined) console.log("warning: no support for --jitter with VMware Player");
  if (timeout !== undefined) console.log("warning: no support for --timeout with VMware Player");
  if (killOnFailure) console.log("warning: no support for --kill-on-failure with VMware Player");

  // Memory must be multiple of 4 MB
  const mem = Math.ceil(rawMem / 4) * 4;

  // Write VMX file
  let vmx = `#! /usr/bin/vmware -G
config.version = 8
guestOS = "linux"
memsize = ${mem}
floppy0.present = FALSE
usb.present = FALSE
sound.present = FALSE
gui.exitAtPowerOff = TRUE
gui.exitOnCLIHLT = TRUE
gui.powerOnAtStartUp = TRUE
`;

  if (serial) {
    vmx += `serial0.present = TRUE
serial0.fileType = "pipe"
serial0.fileName = "pintos.socket"
serial0.pipe.endPoint = "client"
serial0.tryNoRxLoss = "TRUE"
`;
  }

  // Add disks
  for (let i = 0; i < 4; i++) {
    const disk = disks[i];
    if (!disk) continue;

    const device = `ide${Math.floor(i / 2)}:${i % 2}`;
    const pln = `${device}.pln`;

    vmx += `
${device}.present = TRUE
${device}.deviceType = "plainDisk"
${device}.fileName = "${pln}"
`;

    // Create PLN file with random CID
    const cid = Math.floor(Math.random() * 0xffffffff);
    const geom = diskGeometry(disk);

    const plnContent = `version=1
CID=${cid}
parentCID=ffffffff
createType="monolithicFlat"

RW ${geom.CAPACITY} FLAT "${disk}" 0

# The Disk Data Base
#DDB

ddb.adapterType = "ide"
ddb.virtualHWVersion = "4"
ddb.toolsVersion = "2"
ddb.geometry.cylinders = "${geom.C}"
ddb.geometry.heads = "${geom.H}"
ddb.geometry.sectors = "${geom.S}"
`;

    fs.writeFileSync(pln, plnContent);
  }

  fs.writeFileSync("pintos.vmx", vmx);
  fs.chmodSync("pintos.vmx", 0o755);

  // Check for squish-unix
  let squishUnix: string | null = null;
  if (serial) {
    squishUnix = findInPath("squish-unix");
    if (!squishUnix) {
      console.log("warning: can't find squish-unix, so terminal input and output will fail");
    }
  }

  // Build command
  const vmxPath = path.resolve("pintos.vmx");
  const cmd: string[] = [];
  if (squishUnix) {
    cmd.push(squishUnix, "pintos.socket");
  }
  cmd.push("vmplayer", vmxPath);

  console.log(cmd.join(" "));
  await xsystem(cmd);
}
