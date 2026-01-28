#!/usr/bin/env bun
// testsuite-resolution.ts - YAML Test Suite anchor/alias resolution validation
// Tests that --resolve produces same output in streaming and non-streaming modes

import { readFile } from "fs/promises";
import { join } from "path";
import {
  SRCDIR, runFyTool, TapReporter, getTestDirs, getTestId,
  fileExists, readFileSafe, compareOutput, showDiff,
  tempFile, safeUnlink, runFyToolWithStdin
} from "./test-utils";
import { writeFile } from "fs/promises";

const testSuiteDir = join(SRCDIR, "test", "test-suite-data");

// Skip and expected failure lists
const skips: Record<string, string> = {
  "2JQS": "duplicate keys in testcase; cannot load as document",
  "X38W": "duplicate keys after resolution",
};
const xfails: Record<string, string> = {};

// Check if file contains an alias
async function hasAlias(path: string): Promise<boolean> {
  try {
    const content = await readFile(path, "utf-8");
    return /\*[A-Za-z]/.test(content);
  } catch {
    return false;
  }
}

async function main() {
  // Get all test dirs, filter appropriately
  const allTestDirs = await getTestDirs(testSuiteDir, /^[A-Z0-9]{4}$/);
  const testDirs: string[] = [];

  for (const testPath of allTestDirs) {
    if (await fileExists(join(testPath, "error"))) {
      continue; // Skip error tests
    }
    if (!await hasAlias(join(testPath, "in.yaml"))) {
      continue; // Skip tests without aliases
    }
    testDirs.push(testPath);
  }

  const tap = new TapReporter(testDirs.length);

  for (const testPath of testDirs) {
    const testId = getTestId(testPath, testSuiteDir);
    const desc = (await readFileSafe(join(testPath, "===")))?.trim() ?? "";
    const inYaml = join(testPath, "in.yaml");

    // Check skip list
    const skipReason = skips[testId.split("/")[0]] || skips[testId];
    if (skipReason) {
      tap.skip(`${testId} - ${desc}`, skipReason);
      continue;
    }

    // Run dump --resolve, then testsuite (non-streaming)
    const dump1 = await runFyTool(["--dump", "--resolve", inYaml]);
    let testsuite1 = "";
    if (dump1.exitCode === 0) {
      const ts1 = await runFyToolWithStdin(["--testsuite", "--disable-flow-markers"], dump1.stdout);
      testsuite1 = ts1.stdout;
    }

    // Run dump --resolve --streaming, then testsuite
    const dump2 = await runFyTool(["--dump", "--resolve", "--streaming", inYaml]);
    let testsuite2 = "";
    if (dump2.exitCode === 0) {
      const ts2 = await runFyToolWithStdin(["--testsuite", "--disable-flow-markers"], dump2.stdout);
      testsuite2 = ts2.stdout;
    }

    let res: "ok" | "not ok" = "not ok";
    let directive: string | undefined;

    if (dump1.exitCode === 0 && dump2.exitCode === 0) {
      if (compareOutput(testsuite1, testsuite2)) {
        res = "ok";
      } else {
        showDiff(testsuite1, testsuite2);
      }
    }

    // Check xfail list
    const xfailReason = xfails[testId.split("/")[0]] || xfails[testId];
    if (xfailReason) {
      directive = `TODO: ${xfailReason}`;
    }

    if (res === "ok") {
      tap.ok(`${testId} - ${desc}`, directive);
    } else {
      tap.notOk(`${testId} - ${desc}`, directive);
    }
  }

  tap.summary();
  process.exit(tap.exitCode());
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
