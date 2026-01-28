#!/usr/bin/env bun
// libfyaml.ts - Runs the libfyaml-test executable

import { libfyamlTest, TOP_BUILDDIR } from "./test-utils";
import { join } from "path";
import { spawnSync } from "child_process";
import { existsSync } from "fs";

async function main() {
  // Add DLL directory to PATH on Windows
  if (process.platform === "win32") {
    const dllDir = join(TOP_BUILDDIR, "Release");
    process.env.PATH = `${dllDir};${process.env.PATH}`;
  }

  // Check if the test executable exists
  if (!existsSync(libfyamlTest)) {
    console.log("1..0 # SKIP: libfyaml-test executable not found");
    console.log(`# Expected at: ${libfyamlTest}`);
    console.log("# This test requires building the libfyaml-test target");
    process.exit(0);
  }

  const result = spawnSync(libfyamlTest, [], {
    stdio: "inherit",
    env: process.env,
  });

  process.exit(result.status ?? 1);
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
