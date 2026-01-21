# Running GitHub Actions Workflows Locally

This guide shows you how to test GitHub Actions workflows on your local machine using [act](https://github.com/nektos/act).

## Installing act

### Linux

```bash
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash
```

### macOS

```bash
brew install act
```

### Manual Installation

Download from: https://github.com/nektos/act/releases

## Basic Usage

### List All Workflows

```bash
act -l
```

This shows all workflows, jobs, and events they respond to.

### List Jobs in a Specific Workflow

```bash
act -W .github/workflows/ci.yaml -l
act -W .github/workflows/cmake.yaml -l
act -W .github/workflows/freebsd.yaml -l
```

### Run a Specific Workflow

```bash
# Run the Autotools workflow
act -W .github/workflows/ci.yaml

# Run the CMake workflow
act -W .github/workflows/cmake.yaml

# Run a BSD workflow (requires self-hosted runner)
act -W .github/workflows/freebsd.yaml
```

### Run a Specific Job

```bash
# Run just the build job from CMake workflow
act -W .github/workflows/cmake.yaml -j build

# Run the build-with-options job
act -W .github/workflows/cmake.yaml -j build-with-options

# Run the build-with-llvm job
act -W .github/workflows/cmake.yaml -j build-with-llvm
```

### Dry Run (See What Would Happen)

```bash
# See what would run without actually running it
act -W .github/workflows/ci.yaml -n

# Dry run with verbose output
act -W .github/workflows/cmake.yaml -n -v
```

## Simulating Events

### Push Event (Default)

```bash
act push -W .github/workflows/ci.yaml
```

### Pull Request Event

```bash
act pull_request -W .github/workflows/cmake.yaml
```

## Platform Selection

By default, act uses Docker containers. You can specify which platform to emulate:

```bash
# Use Ubuntu (medium size image)
act -P ubuntu-latest=catthehacker/ubuntu:act-latest

# Use a smaller image (faster, but may be missing some tools)
act -P ubuntu-latest=catthehacker/ubuntu:act-20.04

# Use full GitHub-compatible image (large, but most compatible)
act -P ubuntu-latest=catthehacker/ubuntu:full-latest
```

## Important Limitations

### Matrix Builds

Act runs **one matrix combination at a time** by default. Our workflows have matrices, so:

```bash
# This will run only ONE combination from the matrix
act -W .github/workflows/ci.yaml -j build
```

To test all matrix combinations, you'd need to run multiple times or test directly without act.

### Self-Hosted Runners

The BSD workflows use `[self-hosted, <os>]` labels. Act cannot emulate these directly since:
- BSD systems need actual BSD environments
- Self-hosted runners require real hardware/VMs

For BSD testing, use the actual BSD systems or VMs.

### macOS Workflows

Act runs Linux containers, so macOS workflows won't work perfectly:
- Different package managers (brew vs apt)
- Different system behaviors
- Different compiler versions

For accurate macOS testing, use actual macOS or the real GitHub Actions.

## Practical Testing Strategy

### 1. Quick Syntax Check

```bash
# Verify workflow syntax without running
act -W .github/workflows/ci.yaml -n
```

### 2. Test One Job Locally

```bash
# Test the build job to catch obvious issues
act -W .github/workflows/cmake.yaml -j build -P ubuntu-latest=catthehacker/ubuntu:act-latest
```

### 3. Use Local Test Scripts for Real Testing

For actual build and test verification, use the local test scripts instead:

```bash
# Much faster and more reliable than act
./test-local.sh --quick
./test-local.sh --cmake-only
```

### 4. Push to a Branch for Full CI

For comprehensive testing across all platforms and matrix combinations:

```bash
# Create a test branch
git checkout -b test-ci

# Push to GitHub to run real CI
git push origin test-ci
```

## Advanced Usage

### Set Environment Variables

```bash
act -W .github/workflows/ci.yaml --env CC=gcc --env CXX=g++
```

### Use Custom Secrets

```bash
# Create a .secrets file
echo "GITHUB_TOKEN=your_token_here" > .secrets

# Run with secrets
act -W .github/workflows/ci.yaml --secret-file .secrets
```

### Verbose Output

```bash
# See detailed execution logs
act -W .github/workflows/cmake.yaml -v
```

### Specify Runner Image

```bash
# Use specific Docker image
act -W .github/workflows/ci.yaml -P ubuntu-22.04=ubuntu:22.04
```

## Example Workflow Testing Session

Here's a complete example of testing the CMake workflow:

```bash
# 1. Check syntax
act -W .github/workflows/cmake.yaml -n
# ✓ Workflow syntax is valid

# 2. See what jobs would run
act -W .github/workflows/cmake.yaml -l
# Shows: build, build-with-options, build-with-llvm

# 3. Test one specific job (build)
act -W .github/workflows/cmake.yaml -j build -P ubuntu-latest=catthehacker/ubuntu:act-latest
# Runs the build job in Ubuntu container

# 4. If that works, test locally for real
./test-local.sh --cmake-only
# ✓ Full local test with your actual system

# 5. Push to branch for full matrix testing
git push origin HEAD
# Runs all matrix combinations on GitHub
```

## Common Issues and Solutions

### "Docker daemon not running"

```bash
# Start Docker
sudo systemctl start docker  # Linux
# or open Docker Desktop (macOS/Windows)
```

### "Image pull failed"

```bash
# Pull image manually first
docker pull catthehacker/ubuntu:act-latest
```

### "Job failed" with no clear error

```bash
# Run with verbose output
act -W .github/workflows/ci.yaml -v

# Check Docker logs
docker logs <container-id>
```

### "Self-hosted runner" errors

```bash
# BSD workflows won't work with act
# Test on actual BSD systems or use VMs
```

### Matrix only runs one combination

This is expected behavior. To test all combinations:
- Use local test scripts for your platform
- Push to GitHub for full matrix testing
- Or run act multiple times with different env vars

## When to Use act vs Local Scripts

**Use act when:**
- Validating workflow syntax
- Testing workflow logic (steps, conditionals)
- Debugging GitHub Actions specific issues
- Quick smoke test before pushing

**Use local scripts when:**
- Actually testing the build
- Running the full test suite
- Testing on your specific OS/architecture
- Iterating quickly during development

**Use GitHub CI when:**
- Testing all matrix combinations
- Testing on macOS, Windows, BSD
- Final validation before merge
- Testing with different compilers/versions

## Configuration File

You can create a `.actrc` file to set default options:

```bash
# .actrc in project root
-P ubuntu-latest=catthehacker/ubuntu:act-latest
-P ubuntu-22.04=catthehacker/ubuntu:act-22.04
-P ubuntu-20.04=catthehacker/ubuntu:act-20.04
--container-architecture linux/amd64
```

Then just run:
```bash
act -W .github/workflows/ci.yaml
```

## Resources

- **act documentation:** https://github.com/nektos/act
- **Act runner images:** https://github.com/catthehacker/docker_images
- **GitHub Actions docs:** https://docs.github.com/en/actions
- **Local testing guide:** [LOCAL-TESTING.md](LOCAL-TESTING.md)
- **BSD setup:** [BSD-SETUP.md](BSD-SETUP.md)

## Quick Reference

```bash
# List workflows
act -l

# Run specific workflow
act -W .github/workflows/<file>.yaml

# Run specific job
act -W .github/workflows/<file>.yaml -j <job-id>

# Dry run
act -W .github/workflows/<file>.yaml -n

# Verbose
act -W .github/workflows/<file>.yaml -v

# With event
act push -W .github/workflows/<file>.yaml
act pull_request -W .github/workflows/<file>.yaml
```
