# GitHub Actions Workflows

This directory contains CI/CD workflows for libfyaml.

## Workflow Overview

### Full Matrix Workflows (For GitHub CI)

These workflows test across multiple platforms, compilers, and configurations:

- **`ci.yaml`** - Autotools CI with matrix (Ubuntu 20.04/22.04/latest, macOS 13/latest × gcc/clang)
- **`cmake.yaml`** - CMake CI with large matrix (Ubuntu/macOS/Windows × gcc/clang/msvc × Debug/Release)
- **`freebsd.yaml`** - FreeBSD testing (requires self-hosted runner)
- **`openbsd.yaml`** - OpenBSD testing (requires self-hosted runner)
- **`netbsd.yaml`** - NetBSD testing (requires self-hosted runner)

⚠️ **These workflows have large matrices and will cause issues when run with act locally.**

### Single-Platform Workflows (For Local Testing with act)

These workflows test a single platform without matrices, designed for local testing:

- **`ubuntu-latest-test.yaml`** - Ubuntu latest testing (5 jobs, no matrix)
- **`macos-latest-test.yaml`** - macOS latest testing (5 jobs, no matrix)
- **`windows-latest-test.yaml`** - Windows latest testing (4 jobs, no matrix)

✅ **Ubuntu/macOS workflows work well with act for local testing.**
⚠️ **Windows workflow requires GitHub Actions (act cannot emulate Windows).**

## Using Workflows with act

### Full Matrix Workflows

**DO NOT run full matrix workflows with act:**

```bash
# ❌ DON'T DO THIS - will create 20+ containers
act -W .github/workflows/cmake.yaml -j build
```

**Instead, validate syntax only:**

```bash
# ✓ Validate syntax (dry-run)
act -W .github/workflows/cmake.yaml -n
act -W .github/workflows/ci.yaml -n
```

### Single-Platform Workflows

**Run these with act for local testing:**

```bash
# Run all Ubuntu tests
act -W .github/workflows/ubuntu-latest-test.yaml

# Run specific job
act -W .github/workflows/ubuntu-latest-test.yaml -j cmake-gcc-debug

# Run all macOS tests (emulated in Linux container)
act -W .github/workflows/macos-latest-test.yaml

# Run specific macOS job
act -W .github/workflows/macos-latest-test.yaml -j cmake-clang-debug
```

**List available jobs:**

```bash
act -W .github/workflows/ubuntu-latest-test.yaml -l
act -W .github/workflows/macos-latest-test.yaml -l
```

## Workflow Jobs

### ubuntu-latest-test.yaml

5 jobs testing different configurations:

1. **autotools-gcc** - Autotools build with GCC
2. **autotools-clang** - Autotools build with Clang
3. **cmake-gcc-debug** - CMake Debug build with GCC
4. **cmake-clang-release** - CMake Release build with Clang
5. **cmake-with-asan** - CMake with AddressSanitizer

```bash
# Run all 5 jobs
act -W .github/workflows/ubuntu-latest-test.yaml

# Run just the ASAN build
act -W .github/workflows/ubuntu-latest-test.yaml -j cmake-with-asan
```

### macos-latest-test.yaml

5 jobs testing different configurations:

1. **autotools-clang** - Autotools build with Clang (default macOS compiler)
2. **autotools-gcc** - Autotools build with GCC-13
3. **cmake-clang-debug** - CMake Debug build with Clang
4. **cmake-gcc-release** - CMake Release build with GCC-13
5. **cmake-with-llvm** - CMake with LLVM/libclang (reflection support)

```bash
# Run all 5 jobs (emulated in Linux container)
act -W .github/workflows/macos-latest-test.yaml

# Run just the LLVM/reflection build
act -W .github/workflows/macos-latest-test.yaml -j cmake-with-llvm
```

### windows-latest-test.yaml

4 jobs testing different configurations (CMake only, no Autotools):

1. **cmake-msvc-debug** - CMake Debug build with MSVC and Ninja
2. **cmake-msvc-release** - CMake Release build with MSVC and Ninja
3. **cmake-msvc-static-libs** - CMake Release with static libraries
4. **cmake-msvc-vs-generator** - CMake with Visual Studio generator (no Ninja)

```bash
# Windows workflows cannot be run with act (no Windows emulation)
# Use GitHub Actions directly or validate syntax only:
act -W .github/workflows/windows-latest-test.yaml -n

# Trigger manually via GitHub CLI:
gh workflow run windows-latest-test.yaml
```

## Testing Strategy

### During Development

```bash
# 1. Quick local testing (fastest, most reliable)
./test-local.sh --quick

# 2. Validate workflow syntax
act -W .github/workflows/ubuntu-latest-test.yaml -n

# 3. Test specific configuration with act
act -W .github/workflows/ubuntu-latest-test.yaml -j cmake-gcc-debug
```

### Before Committing

```bash
# 1. Full local test suite
./test-local.sh

# 2. Validate all workflow syntax
for f in .github/workflows/*.yaml; do
  act -W "$f" -n
done

# 3. Test one complete workflow with act
act -W .github/workflows/ubuntu-latest-test.yaml
```

### Before Pushing

```bash
# Validate workflows
act -W .github/workflows/ci.yaml -n
act -W .github/workflows/cmake.yaml -n

# Full local testing
./test-local.sh
```

### After Pushing

GitHub Actions will automatically run:
- `ci.yaml` - Full Autotools matrix
- `cmake.yaml` - Full CMake matrix
- Platform-specific test workflows if available

Check results at: https://github.com/<owner>/<repo>/actions

## Workflow Triggers

### Automatic Triggers

All workflows trigger on:
- **Push** to `master` or `main` branches
- **Pull request** to `master` or `main` branches

### Manual Triggers

Single-platform workflows also support manual triggering:

```bash
# Via GitHub UI: Actions → Select workflow → Run workflow

# Via gh CLI:
gh workflow run ubuntu-latest-test.yaml
gh workflow run macos-latest-test.yaml
gh workflow run windows-latest-test.yaml
```

## Debugging Workflows

See comprehensive debugging documentation:

- **[DEBUGGING-WORKFLOWS.md](../../DEBUGGING-WORKFLOWS.md)** - Complete debugging guide
- **[RUN-WORKFLOWS-LOCALLY.md](../../RUN-WORKFLOWS-LOCALLY.md)** - act usage guide
- **[LOCAL-TESTING.md](../../LOCAL-TESTING.md)** - Local testing without act

Quick debug session:

```bash
# Run interactive debugging demo
./debug-workflow.sh

# Check workflow syntax
act -W .github/workflows/ubuntu-latest-test.yaml -n

# Run with verbose output
act -W .github/workflows/ubuntu-latest-test.yaml -v

# Check Docker containers
docker ps -a | grep act-
```

## Best Practices

### DO ✅

- Use single-platform workflows for local act testing
- Validate syntax with dry-run (`-n`) before running
- Use local test scripts for real validation
- Push to GitHub for full matrix testing
- Clean up Docker containers after testing

### DON'T ❌

- Run large matrix workflows with act
- Use act for comprehensive testing
- Rely on act for cross-platform validation
- Leave act containers running after testing

## Common Commands

```bash
# Validate workflow syntax
act -W .github/workflows/ubuntu-latest-test.yaml -n

# Run specific job
act -W .github/workflows/ubuntu-latest-test.yaml -j cmake-gcc-debug

# Run entire workflow
act -W .github/workflows/ubuntu-latest-test.yaml

# List all jobs
act -W .github/workflows/ubuntu-latest-test.yaml -l

# Dry-run all workflows
for f in .github/workflows/*.yaml; do
  echo "Validating $f"
  act -W "$f" -n
done

# Clean up containers
docker ps -a | grep act- | awk '{print $1}' | xargs -r docker rm -f
```

## Troubleshooting

### act runs out of memory

**Problem:** Large matrix workflows create too many containers

**Solution:** Use single-platform workflows instead
```bash
# Don't use: act -W .github/workflows/cmake.yaml
# Use instead: act -W .github/workflows/ubuntu-latest-test.yaml
```

### Workflow fails in act but works on GitHub

**Problem:** act uses Linux containers, may not emulate macOS/Windows correctly

**Solution:**
- Test locally with `./test-local.sh`
- Push to GitHub for real CI testing
- Use single-platform workflows for act validation

### Can't select specific matrix combination

**Problem:** act doesn't support matrix value selection

**Solution:** Use single-platform workflows that don't use matrices

## Additional Resources

- **Testing Guide:** [TESTING-QUICK-REFERENCE.md](../../TESTING-QUICK-REFERENCE.md)
- **BSD Setup:** [BSD-SETUP.md](../../BSD-SETUP.md)
- **act Installation:** [INSTALL-ACT.md](../../INSTALL-ACT.md)
- **Local Testing:** [LOCAL-TESTING.md](../../LOCAL-TESTING.md)
- **GitHub Actions Docs:** https://docs.github.com/en/actions
- **act Documentation:** https://github.com/nektos/act
