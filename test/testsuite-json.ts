#!/usr/bin/env bun
// testsuite-json.ts - YAML Test Suite JSON output validation
// Tests YAML to JSON conversion against test-suite-data

import { $ } from "bun";
import { join } from "path";
import {
  SRCDIR, runFyTool, TapReporter, getTestDirs, getTestId,
  fileExists, readFileSafe, compareOutput, showDiff, tempFile, safeUnlink
} from "./test-utils";
import { writeFile } from "fs/promises";

const testSuiteDir = join(SRCDIR, "test", "test-suite-data");

// Skip and expected failure lists
const skips: Record<string, string> = {
  "UGM3": "Later jq versions rewrite numbers like 12.00 -> 12 which breaks diff",
};
const xfails: Record<string, string> = {
  "C4HZ": "requires schema support which libfyaml does not support yet",
};

// Check if jq is available
async function hasJq(): Promise<boolean> {
  try {
    await $`jq --version`.quiet();
    return true;
  } catch {
    return false;
  }
}

async function sortJson(json: string): Promise<string> {
  const t = tempFile("json");
  try {
    await writeFile(t, json);
    const result = await $`jq --sort-keys . ${t}`.quiet();
    return result.stdout.toString();
  } catch {
    return json;
  } finally {
    await safeUnlink(t);
  }
}

async function main() {
  if (!await hasJq()) {
    console.log("1..0 # SKIP: jq not available");
    process.exit(0);
  }

  // Get all test dirs, filter out error tests and those without in.json
  const allTestDirs = await getTestDirs(testSuiteDir, /^[A-Z0-9]{4}$/);
  const testDirs: string[] = [];

  for (const testPath of allTestDirs) {
    if (await fileExists(join(testPath, "error"))) {
      continue; // Skip error tests
    }
    if (!await fileExists(join(testPath, "in.json"))) {
      continue; // Skip tests without JSON
    }
    testDirs.push(testPath);
  }

  const tap = new TapReporter(testDirs.length);

  for (const testPath of testDirs) {
    const testId = getTestId(testPath, testSuiteDir);
    const desc = (await readFileSafe(join(testPath, "===")))?.trim() ?? "";
    const inYaml = join(testPath, "in.yaml");
    const inJson = join(testPath, "in.json");

    // Check skip list
    const skipReason = skips[testId.split("/")[0]] || skips[testId];
    if (skipReason) {
      tap.skip(`${testId} - ${desc} (JSON)`, skipReason);
      continue;
    }

    // Convert YAML to JSON
    const result = await runFyTool([
      "--dump", "--strip-labels", "--strip-tags", "--strip-doc", "-r", "-mjson", inYaml
    ]);

    let res: "ok" | "not ok" = "not ok";
    let directive: string | undefined;

    if (result.exitCode === 0) {
      // Sort both JSON outputs for comparison
      const yamlJson = await sortJson(result.stdout);
      const expectedJson = await sortJson((await readFileSafe(inJson)) ?? "");

      if (compareOutput(expectedJson, yamlJson)) {
        res = "ok";
      } else {
        showDiff(expectedJson, yamlJson);
      }
    }

    // Check xfail list
    const xfailReason = xfails[testId.split("/")[0]] || xfails[testId];
    if (xfailReason) {
      directive = `TODO: ${xfailReason}`;
    }

    if (res === "ok") {
      tap.ok(`${testId} - ${desc} (JSON)`, directive);
    } else {
      tap.notOk(`${testId} - ${desc} (JSON)`, directive);
    }
  }

  tap.summary();
  process.exit(tap.exitCode());
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
