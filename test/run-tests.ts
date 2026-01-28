#!/usr/bin/env bun
// run-tests.ts - Runs all libfyaml tests
// Usage: bun run run-tests.ts [test-name...]
// Example: bun run run-tests.ts testerrors testemitter

import { spawnSync } from "child_process";
import { join, dirname } from "path";

const testDir = dirname(import.meta.path);

const allTests = [
  "testerrors",
  "testemitter",
  "libfyaml",
  "testsuite",
  "testsuite-evstream",
  "testsuite-json",
  "testsuite-resolution",
  "jsontestsuite",
];

// Parse args
const args = Bun.argv.slice(2);
let testsToRun = args.length > 0 ? args : allTests;

// Handle special flags
if (args.includes("--streaming")) {
  testsToRun = ["testemitter --streaming"];
} else if (args.includes("--restreaming")) {
  testsToRun = ["testemitter --restreaming"];
} else if (args.includes("--help") || args.includes("-h")) {
  console.log("Usage: bun run run-tests.ts [test-name...]");
  console.log("\nAvailable tests:");
  allTests.forEach(t => console.log(`  ${t}`));
  console.log("\nSpecial options:");
  console.log("  --streaming     Run testemitter with --streaming");
  console.log("  --restreaming   Run testemitter with --restreaming");
  process.exit(0);
}

console.log("=".repeat(60));
console.log("libfyaml Test Suite");
console.log("=".repeat(60));
console.log();

let totalPassed = 0;
let totalFailed = 0;
let totalSkipped = 0;

for (const testSpec of testsToRun) {
  const [testName, ...testArgs] = testSpec.split(" ");
  const testFile = join(testDir, `${testName}.ts`);

  console.log(`Running: ${testName}${testArgs.length ? ` ${testArgs.join(" ")}` : ""}`);
  console.log("-".repeat(40));

  const result = spawnSync("bun", ["run", testFile, ...testArgs], {
    stdio: "inherit",
    cwd: testDir,
    env: process.env,
  });

  const exitCode = result.status ?? 1;

  if (exitCode === 0) {
    totalPassed++;
    console.log(`\n[PASS] ${testName}\n`);
  } else {
    totalFailed++;
    console.log(`\n[FAIL] ${testName} (exit code: ${exitCode})\n`);
  }
}

console.log("=".repeat(60));
console.log(`Summary: ${totalPassed} passed, ${totalFailed} failed`);
console.log("=".repeat(60));

process.exit(totalFailed > 0 ? 1 : 0);
