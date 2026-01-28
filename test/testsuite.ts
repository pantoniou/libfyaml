#!/usr/bin/env bun
// testsuite.ts - YAML Test Suite validation
// Tests parsing against test-suite-data

import { readFile } from "fs/promises";
import { join, basename, dirname } from "path";
import {
  SRCDIR, runFyTool, TapReporter, getTestDirs, getTestId,
  fileExists, readFileSafe, compareOutput, showDiff
} from "./test-utils";

const testSuiteDir = join(SRCDIR, "test", "test-suite-data");

// Skip and expected failure lists
const skipList: string[] = [];
const xfailList: string[] = [];

async function main() {
  const testDirs = await getTestDirs(testSuiteDir, /^[A-Z0-9]{4}$/);
  const tap = new TapReporter(testDirs.length);

  for (const testPath of testDirs) {
    const testId = getTestId(testPath, testSuiteDir);
    const desc = (await readFileSafe(join(testPath, "===")))?.trim() ?? "";
    const inYaml = join(testPath, "in.yaml");
    const hasError = await fileExists(join(testPath, "error"));
    const expectedEvent = join(testPath, "test.event");

    // Check skip list
    if (skipList.includes(testId)) {
      tap.skip(`${testId} - ${desc}`, "in skip list");
      continue;
    }

    // Run testsuite
    const result = await runFyTool(["--testsuite", inYaml]);

    let res: "ok" | "not ok" = "not ok";
    let directive: string | undefined;

    if (hasError) {
      // Test is expected to fail
      if (result.exitCode !== 0) {
        res = "ok";
      }
    } else {
      // Test is expected to pass
      if (result.exitCode === 0) {
        const expected = await readFileSafe(expectedEvent);
        if (expected !== null && compareOutput(expected, result.stdout)) {
          res = "ok";
        } else if (expected !== null) {
          showDiff(expected, result.stdout);
        }
      }
    }

    // Check xfail list
    if (xfailList.includes(testId)) {
      directive = "TODO: known failure";
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
