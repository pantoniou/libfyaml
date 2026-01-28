 import { $ } from "bun";
  import { readdir, readFile, writeFile, unlink, stat } from "fs/promises";
  import { join, dirname, basename } from "path";
  import { tmpdir } from "os";
  import { existsSync } from "fs";

  // Directory paths
  export const SRCDIR = process.env.SRCDIR || dirname(import.meta.dir);
  export const TOP_BUILDDIR = process.env.TOP_BUILDDIR || join(SRCDIR, "build");

  // Tool paths - handle both VS (Release subfolder) and Ninja (flat) builds
  function getToolPath(name: string): string {
    if (process.platform === "win32") {
      const vsPath = join(TOP_BUILDDIR, "Release", `${name}.exe`);
      const ninjaPath = join(TOP_BUILDDIR, `${name}.exe`);
      return existsSync(vsPath) ? vsPath : ninjaPath;
    }
    return join(TOP_BUILDDIR, name);
  }

  export const fyTool = getToolPath("fy-tool");
  export const libfyamlTest = getToolPath("libfyaml-test");

  // Add DLL directory to PATH on Windows
  if (process.platform === "win32") {
    const vsDir = join(TOP_BUILDDIR, "Release");
    const ninjaDir = TOP_BUILDDIR;
    process.env.PATH = `${vsDir};${ninjaDir};${process.env.PATH}`;
  }

  // Generate a unique temp file path
  export function tempFile(prefix: string): string {
    const name = `${prefix}-${Date.now()}-${Math.random().toString(36).slice(2)}.tmp`;
    return join(tmpdir(), name);
  }

  // Safely delete a file
  export async function safeUnlink(path: string): Promise<void> {
    try { await unlink(path); } catch { }
  }

  // Read file content safely
  export async function readFileSafe(path: string): Promise<string | null> {
    try {
      return await readFile(path, "utf-8");
    } catch {
      return null;
    }
  }

  // Check if file exists
  export async function fileExists(path: string): Promise<boolean> {
    try {
      await stat(path);
      return true;
    } catch {
      return false;
    }
  }

  // Run fy-tool with arguments
  export async function runFyTool(args: string[]): Promise<{ exitCode: number; stdout: string; stderr: string }> {
    try {
      const result = await $`${fyTool} ${args}`.quiet();
      return {
        exitCode: 0,
        stdout: result.stdout.toString(),
        stderr: result.stderr.toString(),
      };
    } catch (err: any) {
      return {
        exitCode: err.exitCode ?? 1,
        stdout: err.stdout?.toString() ?? "",
        stderr: err.stderr?.toString() ?? "",
      };
    }
  }

  // Run fy-tool with stdin input
  export async function runFyToolWithStdin(args: string[], stdin: string): Promise<{ exitCode: number; stdout: string; stderr: string }> {
    const tmpIn = tempFile("stdin");
    try {
      await writeFile(tmpIn, stdin);
      const result = await runFyTool([...args, tmpIn]);
      return result;
    } finally {
      await safeUnlink(tmpIn);
    }
  }

  // Get test directories matching pattern (e.g., test-suite-data/XXXX/)
  export async function getTestDirs(baseDir: string, pattern: RegExp): Promise<string[]> {
    const results: string[] = [];

    try {
      const entries = await readdir(baseDir, { withFileTypes: true });
      for (const entry of entries) {
        if (entry.isDirectory() && pattern.test(entry.name)) {
          const testPath = join(baseDir, entry.name);

          // Check for base test
          if (await fileExists(join(testPath, "==="))) {
            results.push(testPath);
          }

          // Check for subtests (00-99 or 000-999)
          try {
            const subEntries = await readdir(testPath, { withFileTypes: true });
            for (const subEntry of subEntries) {
              if (subEntry.isDirectory() && /^\d{2,3}$/.test(subEntry.name)) {
                const subPath = join(testPath, subEntry.name);
                if (await fileExists(join(subPath, "==="))) {
                  results.push(subPath);
                }
              }
            }
          } catch { }
        }
      }
    } catch { }

    return results.sort();
  }

  // Get test ID from path (e.g., "test-suite-data/ABCD/01" -> "ABCD/01")
  export function getTestId(testPath: string, baseDir: string): string {
    const rel = testPath.replace(baseDir, "").replace(/^[\/\\]+/, "").replace(/[\/\\]+$/, "");
    return rel.replace(/\\/g, "/");
  }

  // TAP output helpers
  export class TapReporter {
    private count = 0;
    private passed = 0;
    private failed = 0;
    private current = 0;

    constructor(count: number) {
      this.count = count;
      console.log(`1..${count}`);
    }

    ok(description: string, directive?: string): void {
      this.current++;
      this.passed++;
      const dir = directive ? ` # ${directive}` : "";
      console.log(`ok ${this.current} ${description}${dir}`);
    }

    notOk(description: string, directive?: string): void {
      this.current++;
      this.failed++;
      const dir = directive ? ` # ${directive}` : "";
      console.log(`not ok ${this.current} ${description}${dir}`);
    }

    skip(description: string, reason: string): void {
      this.current++;
      this.passed++;
      console.log(`ok ${this.current} ${description} # SKIP: ${reason}`);
    }

    comment(msg: string): void {
      console.log(`# ${msg}`);
    }

    summary(): void {
      console.log(`# Passed: ${this.passed}, Failed: ${this.failed}, Total: ${this.count}`);
    }

    exitCode(): number {
      return this.failed > 0 ? 1 : 0;
    }
  }

  // Compare two strings, return true if equal
  export function compareOutput(expected: string, actual: string): boolean {
    return expected === actual;
  }

  // Show diff for debugging
  export function showDiff(expected: string, actual: string, maxLines = 5): void {
    const expLines = expected.split("\n").slice(0, maxLines);
    const actLines = actual.split("\n").slice(0, maxLines);
    console.log("# --- Expected ---");
    expLines.forEach(line => console.log(`# ${line}`));
    console.log("# --- Got ---");
    actLines.forEach(line => console.log(`# ${line}`));
  }