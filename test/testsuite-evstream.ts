#!/usr/bin/env bun
// testsuite-evstream.ts - YAML Test Suite with document-event-stream mode
// Tests parsing against test-suite-data using --document-event-stream

import { join } from "path";
import {
  SRCDIR, runFyTool, TapReporter, getTestDirs, getTestId,
  fileExists, readFileSafe, compareOutput, showDiff
} from "./test-utils";

const testSuiteDir = join(SRCDIR, "test", "test-suite-data");

// Skip and expected failure lists
const skips: Record<string, string> = {
  "2JQS": "duplicate keys in testcase; cannot load as document",
};
const xfails: Record<string, string> = {};

async function main() {
  // Get all test dirs, filter out error tests
  const allTestDirs = await getTestDirs(testSuiteDir, /^[A-Z0-9]{4}$/);
  const testDirs: string[] = [];

  for (const testPath of allTestDirs) {
    if (await fileExists(join(testPath, "error"))) {
      continue; // Skip error tests
    }
    testDirs.push(testPath);
  }

  const tap = new TapReporter(testDirs.length);

  for (const testPath of testDirs) {
    const testId = getTestId(testPath, testSuiteDir);
    const desc = (await readFileSafe(join(testPath, "===")))?.trim() ?? "";
    const inYaml = join(testPath, "in.yaml");
    const expectedEvent = join(testPath, "test.event");

    // Check skip list
    const skipReason = skips[testId.split("/")[0]] || skips[testId];
    if (skipReason) {
      tap.skip(`${testId} - ${desc}`, skipReason);
      continue;
    }

    // Run testsuite with document-event-stream
    const result = await runFyTool(["--testsuite", "--document-event-stream", inYaml]);

    let res: "ok" | "not ok" = "not ok";
    let directive: string | undefined;

    if (result.exitCode === 0) {
      const expected = await readFileSafe(expectedEvent);
      if (expected !== null && compareOutput(expected, result.stdout)) {
        res = "ok";
      } else if (expected !== null) {
        showDiff(expected, result.stdout);
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
