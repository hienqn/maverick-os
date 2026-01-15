/**
 * GDB/MI (Machine Interface) output parser
 *
 * Parses GDB's machine interface output format into structured TypeScript objects.
 * Reference: https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Output-Syntax.html
 *
 * Output format:
 *   result-record: [token] "^" result-class ("," result)*
 *   exec-async:    [token] "*" async-class ("," result)*
 *   status-async:  [token] "+" async-class ("," result)*
 *   notify-async:  [token] "=" async-class ("," result)*
 *   console-stream: "~" c-string
 *   target-stream:  "@" c-string
 *   log-stream:     "&" c-string
 */

import type { MiRecord, MiRecordType } from "./debug-types";

/**
 * Parse a single line of GDB/MI output
 */
export function parseMiLine(line: string): MiRecord | null {
  line = line.trim();
  if (line === "" || line === "(gdb)") {
    return null;
  }

  // Check for stream outputs first (they start with ~, @, or &)
  if (line.startsWith("~")) {
    return { type: "console", text: parseCString(line.slice(1)) };
  }
  if (line.startsWith("@")) {
    return { type: "target", text: parseCString(line.slice(1)) };
  }
  if (line.startsWith("&")) {
    return { type: "log", text: parseCString(line.slice(1)) };
  }

  // Parse token (optional numeric prefix)
  let token: number | undefined;
  let rest = line;
  const tokenMatch = line.match(/^(\d+)/);
  if (tokenMatch) {
    token = parseInt(tokenMatch[1], 10);
    rest = line.slice(tokenMatch[1].length);
  }

  // Determine record type from first character
  let type: MiRecordType;
  switch (rest[0]) {
    case "^":
      type = "result";
      break;
    case "*":
      type = "exec";
      break;
    case "+":
      type = "status";
      break;
    case "=":
      type = "notify";
      break;
    default:
      // Unknown format, return as console text
      return { type: "console", text: line };
  }

  rest = rest.slice(1);

  // Parse class and results
  const commaIndex = rest.indexOf(",");
  let recordClass: string;
  let resultsStr: string;

  if (commaIndex === -1) {
    recordClass = rest;
    resultsStr = "";
  } else {
    recordClass = rest.slice(0, commaIndex);
    resultsStr = rest.slice(commaIndex + 1);
  }

  // Parse results into data object
  const data = resultsStr ? parseResults(resultsStr) : undefined;

  return { type, token, class: recordClass, data };
}

/**
 * Parse multiple lines of GDB/MI output
 */
export function parseMiOutput(output: string): MiRecord[] {
  const records: MiRecord[] = [];
  for (const line of output.split("\n")) {
    const record = parseMiLine(line);
    if (record) {
      records.push(record);
    }
  }
  return records;
}

/**
 * Parse a C-style string (with escapes)
 */
export function parseCString(str: string): string {
  if (!str.startsWith('"')) {
    return str;
  }

  let result = "";
  let i = 1; // Skip opening quote
  while (i < str.length) {
    if (str[i] === '"') {
      break; // Closing quote
    }
    if (str[i] === "\\") {
      i++;
      if (i >= str.length) break;
      switch (str[i]) {
        case "n":
          result += "\n";
          break;
        case "t":
          result += "\t";
          break;
        case "r":
          result += "\r";
          break;
        case "\\":
          result += "\\";
          break;
        case '"':
          result += '"';
          break;
        default:
          result += str[i];
      }
    } else {
      result += str[i];
    }
    i++;
  }
  return result;
}

/**
 * Parse a results string into a data object
 * Format: name=value,name=value,...
 */
function parseResults(str: string): Record<string, unknown> {
  const result: Record<string, unknown> = {};
  const parser = new ResultParser(str);

  while (!parser.eof()) {
    const name = parser.readName();
    if (!name) break;

    if (!parser.expect("=")) break;

    const value = parser.readValue();
    result[name] = value;

    // Check for comma separator
    if (parser.peek() === ",") {
      parser.advance();
    }
  }

  return result;
}

/**
 * Helper class for parsing MI results
 */
class ResultParser {
  private pos = 0;
  constructor(private str: string) {}

  eof(): boolean {
    return this.pos >= this.str.length;
  }

  peek(): string {
    return this.str[this.pos] || "";
  }

  advance(): void {
    this.pos++;
  }

  expect(ch: string): boolean {
    if (this.peek() === ch) {
      this.advance();
      return true;
    }
    return false;
  }

  readName(): string {
    let name = "";
    while (!this.eof()) {
      const ch = this.peek();
      if (/[a-zA-Z0-9_-]/.test(ch)) {
        name += ch;
        this.advance();
      } else {
        break;
      }
    }
    return name;
  }

  readValue(): unknown {
    const ch = this.peek();

    if (ch === '"') {
      return this.readString();
    } else if (ch === "{") {
      return this.readTuple();
    } else if (ch === "[") {
      return this.readList();
    } else {
      // Bare value (shouldn't happen in valid MI output, but handle it)
      return this.readBareValue();
    }
  }

  readString(): string {
    if (!this.expect('"')) return "";

    let result = "";
    while (!this.eof()) {
      const ch = this.peek();
      this.advance();

      if (ch === '"') {
        break;
      } else if (ch === "\\") {
        const escaped = this.peek();
        this.advance();
        switch (escaped) {
          case "n":
            result += "\n";
            break;
          case "t":
            result += "\t";
            break;
          case "r":
            result += "\r";
            break;
          case "\\":
            result += "\\";
            break;
          case '"':
            result += '"';
            break;
          default:
            result += escaped;
        }
      } else {
        result += ch;
      }
    }
    return result;
  }

  readTuple(): Record<string, unknown> {
    if (!this.expect("{")) return {};

    const result: Record<string, unknown> = {};
    while (!this.eof() && this.peek() !== "}") {
      const name = this.readName();
      if (!name) break;

      if (!this.expect("=")) break;

      const value = this.readValue();
      result[name] = value;

      if (this.peek() === ",") {
        this.advance();
      }
    }
    this.expect("}");
    return result;
  }

  readList(): unknown[] {
    if (!this.expect("[")) return [];

    const result: unknown[] = [];
    while (!this.eof() && this.peek() !== "]") {
      // Lists can contain values or name=value pairs
      // Check if this looks like a named element
      const savedPos = this.pos;
      const name = this.readName();

      if (name && this.peek() === "=") {
        // It's a named element, read as tuple-like
        this.advance(); // skip =
        const value = this.readValue();
        result.push({ [name]: value });
      } else {
        // Not a named element, reset and read as plain value
        this.pos = savedPos;
        const value = this.readValue();
        result.push(value);
      }

      if (this.peek() === ",") {
        this.advance();
      }
    }
    this.expect("]");
    return result;
  }

  readBareValue(): string {
    let value = "";
    while (!this.eof()) {
      const ch = this.peek();
      if (ch === "," || ch === "}" || ch === "]") {
        break;
      }
      value += ch;
      this.advance();
    }
    return value;
  }
}

/**
 * Find a specific async record by class
 */
export function findAsyncRecord(records: MiRecord[], asyncClass: string): MiRecord | undefined {
  return records.find((r) => (r.type === "exec" || r.type === "notify") && r.class === asyncClass);
}

/**
 * Find the result record
 */
export function findResultRecord(records: MiRecord[]): MiRecord | undefined {
  return records.find((r) => r.type === "result");
}

/**
 * Check if result indicates success
 */
export function isResultDone(records: MiRecord[]): boolean {
  const result = findResultRecord(records);
  return result?.class === "done";
}

/**
 * Check if result indicates error
 */
export function isResultError(records: MiRecord[]): boolean {
  const result = findResultRecord(records);
  return result?.class === "error";
}

/**
 * Get error message from result
 */
export function getErrorMessage(records: MiRecord[]): string | undefined {
  const result = findResultRecord(records);
  if (result?.class === "error" && result.data) {
    return result.data.msg as string | undefined;
  }
  return undefined;
}

/**
 * Collect all console output
 */
export function collectConsoleOutput(records: MiRecord[]): string {
  return records
    .filter((r) => r.type === "console")
    .map((r) => r.text || "")
    .join("");
}

/**
 * Parse a stopped async record into structured data
 */
export function parseStoppedRecord(record: MiRecord): {
  reason: string;
  frame?: {
    addr: string;
    func: string;
    file?: string;
    fullname?: string;
    line?: number;
  };
  bkptno?: number;
  wpt?: { number: number; exp: string };
  value?: { old: string; new: string };
  signalName?: string;
  signalMeaning?: string;
} | null {
  if (record.type !== "exec" || record.class !== "stopped" || !record.data) {
    return null;
  }

  const data = record.data;
  const result: ReturnType<typeof parseStoppedRecord> = {
    reason: (data.reason as string) || "unknown",
  };

  // Parse frame information
  if (data.frame && typeof data.frame === "object") {
    const frame = data.frame as Record<string, unknown>;
    result.frame = {
      addr: (frame.addr as string) || "0x0",
      func: (frame.func as string) || "??",
      file: frame.file as string | undefined,
      fullname: frame.fullname as string | undefined,
      line: frame.line ? parseInt(frame.line as string, 10) : undefined,
    };
  }

  // Parse breakpoint number
  if (data.bkptno) {
    result.bkptno = parseInt(data.bkptno as string, 10);
  }

  // Parse watchpoint info
  if (data.wpt && typeof data.wpt === "object") {
    const wpt = data.wpt as Record<string, unknown>;
    result.wpt = {
      number: parseInt(wpt.number as string, 10),
      exp: wpt.exp as string,
    };
  }

  // Parse value change (for watchpoints)
  if (data.value && typeof data.value === "object") {
    const value = data.value as Record<string, unknown>;
    result.value = {
      old: (value.old as string) || "",
      new: (value.new as string) || "",
    };
  }

  // Parse signal info
  if (data["signal-name"]) {
    result.signalName = data["signal-name"] as string;
  }
  if (data["signal-meaning"]) {
    result.signalMeaning = data["signal-meaning"] as string;
  }

  return result;
}
