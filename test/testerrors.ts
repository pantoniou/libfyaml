#!/usr/bin/env bun
// testerrors.ts - Tests error handling in libfyaml

import { readdir } from "fs/promises";
import { join } from "path";
import { SRCDIR, runFyTool, TapReporter, readFileSafe } from "./test-utils";

const testErrorsDir = join(SRCDIR, "test", "test-errors");

async function main() {
  const entries = await readdir(testErrorsDir, { withFileTypes: true });
  const testDirs = entries
    .filter(e => e.isDirectory() && /^[A-Z0-9]{4}$/.test(e.name))
    .map(e => e.name)
    .sort();

  const tap = new TapReporter(testDirs.length);

  for (const tdir of testDirs) {
    const testPath = join(testErrorsDir, tdir);
    const desc = (await readFileSafe(join(testPath, "===")))?.trim() ?? "";
    const inYaml = join(testPath, "in.yaml");

    // Run fy-tool --dump -r and check for error (non-zero exit)
    const result = await runFyTool(["--dump", "-r", inYaml]);

    // Error tests should fail (non-zero exit code)
    if (result.exitCode !== 0) {
      tap.ok(`${tdir} - ${desc}`);
    } else {
      tap.notOk(`${tdir} - ${desc}`);
    }
  }

  tap.summary();
  process.exit(tap.exitCode());
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
