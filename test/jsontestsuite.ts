#!/usr/bin/env bun
// jsontestsuite.ts - JSON Test Suite validation
// Tests parsing of JSON files from json-test-suite-data

import { readdir } from "fs/promises";
import { join } from "path";
import { SRCDIR, runFyTool, TapReporter } from "./test-utils";

const jsonTestDir = join(SRCDIR, "test", "json-test-suite-data", "test_parsing");

interface TestFile {
  path: string;
  name: string;
  type: "pass" | "fail" | "impl";
}

async function main() {
  const entries = await readdir(jsonTestDir, { withFileTypes: true });

  const tests: TestFile[] = [];

  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith(".json")) continue;

    const name = entry.name;
    let type: TestFile["type"];

    if (name.startsWith("y_")) {
      type = "pass";
    } else if (name.startsWith("n_")) {
      type = "fail";
    } else if (name.startsWith("i_")) {
      type = "impl";
    } else {
      continue;
    }

    tests.push({
      path: join(jsonTestDir, name),
      name,
      type,
    });
  }

  tests.sort((a, b) => a.name.localeCompare(b.name));

  const tap = new TapReporter(tests.length);

  for (const test of tests) {
    const result = await runFyTool(["--testsuite", "--streaming", test.path]);

    if (test.type === "pass") {
      // Expected to pass
      if (result.exitCode === 0) {
        tap.ok(test.name);
      } else {
        tap.notOk(test.name);
      }
    } else if (test.type === "fail") {
      // Expected to fail
      if (result.exitCode !== 0) {
        tap.ok(test.name);
      } else {
        tap.notOk(test.name);
      }
    } else {
      // Implementation defined - always OK but report result
      const ires = result.exitCode === 0 ? "i-pass" : "i-fail";
      tap.ok(`${ires} ${test.name}`);
    }
  }

  tap.summary();
  process.exit(tap.exitCode());
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
