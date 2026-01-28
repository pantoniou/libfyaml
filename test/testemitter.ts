#!/usr/bin/env bun
// testemitter.ts - Tests YAML emitter round-trip functionality
// Usage: bun run testemitter.ts [--streaming] [--restreaming]

import { readdir, writeFile } from "fs/promises";
import { join } from "path";
import { parseArgs } from "util";
import {
  SRCDIR, runFyTool, TapReporter, tempFile, safeUnlink,
  compareOutput, showDiff
} from "./test-utils";

const { values: args } = parseArgs({
  args: Bun.argv.slice(2),
  options: {
    streaming: { type: "boolean", default: false },
    restreaming: { type: "boolean", default: false },
  },
});

const emitterExamplesDir = join(SRCDIR, "test", "emitter-examples");

async function main() {
  // Build extra dump args
  const extraDumpArgs: string[] = [];
  if (args.streaming) {
    extraDumpArgs.push("--streaming");
  }
  if (args.restreaming) {
    extraDumpArgs.push("--streaming", "--recreating");
  }

  // Get all yaml files
  const entries = await readdir(emitterExamplesDir, { withFileTypes: true });
  const yamlFiles = entries
    .filter(e => e.isFile() && e.name.endsWith(".yaml"))
    .map(e => e.name)
    .sort();

  const tap = new TapReporter(yamlFiles.length);

  for (const tf of yamlFiles) {
    const filePath = join(emitterExamplesDir, tf);
    const t3 = tempFile("emitter-dumped");

    try {
      // First pass: get testsuite output of original
      const result1 = await runFyTool(["--testsuite", "--disable-flow-markers", filePath]);
      if (result1.exitCode !== 0) {
        tap.notOk(tf);
        continue;
      }
      const original = result1.stdout;

      // Second pass: dump to temp file
      const dumpResult = await runFyTool(["--dump", ...extraDumpArgs, filePath]);
      if (dumpResult.exitCode !== 0) {
        tap.notOk(tf);
        continue;
      }
      await writeFile(t3, dumpResult.stdout);

      // Parse the dumped output from file
      const result2 = await runFyTool(["--testsuite", "--disable-flow-markers", t3]);
      if (result2.exitCode !== 0) {
        tap.notOk(tf);
        continue;
      }
      const reparsed = result2.stdout;

      // Compare outputs
      if (compareOutput(original, reparsed)) {
        tap.ok(tf);
      } else {
        showDiff(original, reparsed);
        tap.notOk(tf);
      }
    } catch {
      tap.notOk(tf);
    } finally {
      await safeUnlink(t3);
    }
  }

  tap.summary();
  process.exit(tap.exitCode());
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
