# Debugging GitHub Actions Workflows with act

This guide covers debugging workflow issues when using act locally.

## Common Issue: Matrix Explosion

### Problem

When running workflows with large matrices, act tries to execute ALL combinations simultaneously:

```bash
act -W .github/workflows/cmake.yaml -j build
```

**What happens:**
- CMake workflow has 22 matrix combinations
- act creates 22 Docker containers at once
- Each installs dependencies and builds simultaneously
- System runs out of memory (exit code 137)
- Containers left running consuming resources

**Evidence:**
```bash
$ docker ps -q | wc -l
19  # All matrix combinations running at once!
```

### Root Cause

act limitation: No built-in way to run matrix combinations one at a time or select specific combinations.

## Solutions

### 1. Dry Run (Validate Syntax Only)

**Best for:** Checking workflow syntax before pushing

```bash
# Validate CMake workflow
act -W .github/workflows/cmake.yaml -n

# Validate all workflows
act -W .github/workflows/ci.yaml -n
act -W .github/workflows/cmake.yaml -n
act -W .github/workflows/freebsd.yaml -n
```

**Output:**
- Shows what would run
- Validates YAML syntax
- Checks if steps are defined correctly
- Doesn't actually execute anything
- Fast and safe

### 2. Use Local Test Scripts (Recommended)

**Best for:** Actual build and test verification

```bash
# Much faster and more reliable than act
./test-local.sh

# Quick mode
./test-local.sh --quick

# CMake only
./test-local.sh --cmake-only

# With specific build type
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

**Advantages:**
- Runs on your actual system
- No Docker overhead
- No matrix explosion
- Real test results
- Much faster

### 3. Create Test-Specific Workflow

Create a simplified workflow for local testing:

```yaml
# .github/workflows/cmake-test.yaml
name: CMake Test (for local act testing)

on: [push]

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v4

    - name: Install dependencies
      run: |
        sudo apt-get update -qq
        sudo apt-get install -y cmake ninja-build pkg-config check libyaml-dev

    - name: Configure
      run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

    - name: Build
      run: cmake --build build

    - name: Test
      working-directory: build
      run: ctest --output-on-failure
```

Then run:
```bash
act -W .github/workflows/cmake-test.yaml
```

### 4. Clean Up Before Running

**Always clean up Docker containers before running act:**

```bash
# Stop and remove all act containers
docker ps -a | grep act- | awk '{print $1}' | xargs -r docker rm -f

# Or stop all containers
docker ps -q | xargs -r docker stop
```

### 5. Monitor Resource Usage

**Before running large matrices:**

```bash
# Check available memory
free -h

# Monitor during execution
watch -n 1 'docker ps | wc -l; free -h | grep Mem'

# Set Docker memory limits
# Edit /etc/docker/daemon.json or Docker Desktop settings
```

## Debugging Specific Workflows

### CMake Workflow

**Matrix size:** 22 combinations

**Problems:**
- Too many simultaneous containers
- Long dependency installation
- Resource exhaustion

**Solution:**
```bash
# Don't use: act -W .github/workflows/cmake.yaml -j build

# Use instead:
act -W .github/workflows/cmake.yaml -n          # Dry run
./test-local.sh --cmake-only                     # Real test
```

### Autotools Workflow

**Matrix size:** 10 combinations

**Problems:**
- Multiple OS versions
- Bootstrap takes time
- Distcheck is slow

**Solution:**
```bash
# Dry run only
act -W .github/workflows/ci.yaml -n

# Real testing
./test-local.sh --autotools-only
```

### BSD Workflows

**Problem:** Self-hosted runners required

**Solution:**
```bash
# Cannot run with act at all
# These workflows need actual BSD systems

# For testing:
# 1. Check syntax only
act -W .github/workflows/freebsd.yaml -n

# 2. Test on actual BSD system
# See BSD-SETUP.md
```

## Best Practices

### 1. Workflow Design for act Compatibility

**DO:**
- Keep matrices small (<5 combinations)
- Use separate jobs instead of large matrices
- Create test-specific simplified workflows
- Document act limitations in comments

**DON'T:**
- Create large matrices (>10 combinations)
- Rely on act for comprehensive testing
- Use act for cross-platform testing

### 2. Testing Strategy

```
┌─────────────────────────────────────┐
│ 1. Local Script Testing             │
│    ./test-local.sh                  │
│    ✓ Fast, reliable                 │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│ 2. Workflow Syntax Validation       │
│    act -W <workflow>.yaml -n        │
│    ✓ Validates YAML                 │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│ 3. Single Combination Test          │
│    act -W <simplified>.yaml         │
│    ✓ Tests Docker environment       │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│ 4. Push to GitHub                   │
│    git push                         │
│    ✓ Full matrix on GitHub          │
└─────────────────────────────────────┘
```

### 3. Debugging Checklist

Before running `act`:

- [ ] Check available RAM: `free -h`
- [ ] Clean up containers: `docker ps -a`
- [ ] Check workflow matrix size
- [ ] Use dry-run first: `act -W <file> -n`
- [ ] Consider using local scripts instead

If act fails:

- [ ] Check exit code (137 = OOM)
- [ ] Check Docker logs: `docker ps -a`
- [ ] Clean up: `docker ps -q | xargs docker stop`
- [ ] Try simplified workflow
- [ ] Use local testing instead

## Common Errors and Solutions

### Exit Code 137 (OOM Kill)

**Error:**
```
Error: Exit code 137
```

**Cause:** Out of memory, too many containers

**Solution:**
```bash
# Stop all containers
docker ps -q | xargs docker stop

# Use local testing instead
./test-local.sh

# Or dry-run only
act -W .github/workflows/cmake.yaml -n
```

### "Error: no matching runner"

**Error:**
```
Error: no matching runner found for macos-latest
```

**Cause:** act uses Linux containers, can't emulate macOS/Windows

**Solution:**
```bash
# macOS/Windows runners won't work properly with act
# Use real GitHub CI or actual systems
```

### "Permission denied" in Container

**Error:**
```
npm ERR! Error: EACCES: permission denied
```

**Cause:** User permission issues in container

**Solution:**
```bash
# This is an act limitation
# Use local testing instead
./test-local.sh
```

### Workflow Hangs

**Symptom:** act appears stuck

**Solution:**
```bash
# Check running containers
docker ps

# Check container logs
docker logs <container-id>

# Force stop after timeout
timeout 300 act -W <workflow>.yaml || docker ps -q | xargs docker stop
```

## Resource Limits

### Set Docker Limits

Edit Docker daemon config (`/etc/docker/daemon.json` or Docker Desktop):

```json
{
  "default-ulimits": {
    "nofile": {
      "Name": "nofile",
      "Hard": 64000,
      "Soft": 64000
    }
  },
  "max-concurrent-downloads": 3,
  "max-concurrent-uploads": 5
}
```

### Limit act Concurrency

act doesn't have built-in concurrency limits, so:

```bash
# Run workflows one at a time
act -W .github/workflows/ci.yaml
# Wait for completion
act -W .github/workflows/cmake.yaml

# Don't run multiple acts simultaneously
```

## Viewing Container Logs

```bash
# List all act containers
docker ps -a | grep act-

# View logs of specific container
docker logs <container-id>

# Follow logs in real-time
docker logs -f <container-id>

# Last 50 lines
docker logs --tail 50 <container-id>
```

## Summary

### ✅ What act is Good For

- Validating workflow syntax (`-n` dry-run)
- Testing simple workflows (no matrix)
- Quick smoke tests
- Debugging workflow logic

### ❌ What act is NOT Good For

- Large matrix builds (>5 combinations)
- Cross-platform testing (macOS, Windows, BSD)
- Comprehensive testing
- Production validation

### 🎯 Recommended Approach

```bash
# For development: Use local scripts
./test-local.sh

# For workflow validation: Use dry-run
act -W .github/workflows/cmake.yaml -n

# For comprehensive testing: Push to GitHub
git push origin HEAD
```

## Quick Reference

```bash
# Dry run all workflows
for f in .github/workflows/*.yaml; do
  echo "Validating $f"
  act -W "$f" -n
done

# Clean up all act containers
docker ps -a | grep act- | awk '{print $1}' | xargs -r docker rm -f

# Monitor resources during act
watch -n 1 'echo "Containers: $(docker ps -q | wc -l)"; free -h | grep Mem'

# Safe testing approach
act -W .github/workflows/cmake.yaml -n && ./test-local.sh --quick
```

## Getting Help

- **act issues:** https://github.com/nektos/act/issues
- **Docker limits:** https://docs.docker.com/config/containers/resource_constraints/
- **Local testing:** [LOCAL-TESTING.md](LOCAL-TESTING.md)
- **Quick reference:** [TESTING-QUICK-REFERENCE.md](TESTING-QUICK-REFERENCE.md)
