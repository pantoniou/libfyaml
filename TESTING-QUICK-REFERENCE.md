# Testing Quick Reference

Quick reference for testing libfyaml locally and with GitHub Actions.

## Local Testing (Recommended for Development)

### Quick Start

```bash
# Install dependencies first time
./install-deps.sh

# Run full test suite (both Autotools and CMake)
./test-local.sh

# Quick test (faster, skips distcheck)
./test-local.sh --quick

# Test only CMake
./test-local.sh --cmake-only
```

### Manual Testing

```bash
# Autotools
./bootstrap.sh && ./configure && make -j$(nproc) && make check

# CMake
cmake -B build -G Ninja && cmake --build build && cd build && ctest
```

## GitHub Actions Workflows Local Testing

### Install act

```bash
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash
```

### Run Workflows Locally

```bash
# List all workflows
act -l

# Dry run (validate syntax)
act -W .github/workflows/cmake.yaml -n

# Run specific workflow
act -W .github/workflows/cmake.yaml -j build
```

**Limitations:**
- Matrix runs ONE combination at a time
- BSD/macOS workflows won't work correctly (use VMs or real hardware)
- Slower than native local testing

## Platform-Specific Testing

### Linux (Ubuntu)
```bash
./test-local.sh
```

### macOS
```bash
./test-local.sh
```

### FreeBSD
See [BSD-SETUP.md](BSD-SETUP.md)
```bash
gmake check  # Autotools
cd build && ctest  # CMake
```

### OpenBSD
See [BSD-SETUP.md](BSD-SETUP.md)
```bash
gmake check  # Autotools
cd build && ctest  # CMake
```

### NetBSD
See [BSD-SETUP.md](BSD-SETUP.md)
```bash
# make check doesn't work with Autotools on NetBSD
cd build && ctest  # Use CMake for testing
```

## Testing Strategy

```
Local Development
    ↓
./test-local.sh --quick
    ↓
./test-local.sh (full)
    ↓
act -W .github/workflows/<file>.yaml -n (validate)
    ↓
git push (runs full CI on GitHub)
    ↓
Review Results
```

## Build Options

### CMake Options
```bash
cmake -B build \
  -DENABLE_ASAN=ON              # Memory debugging
  -DENABLE_STATIC_TOOLS=ON      # Static executables
  -DENABLE_PORTABLE_TARGET=ON   # No SIMD optimizations
  -DBUILD_SHARED_LIBS=OFF       # Static libraries
  -DCMAKE_BUILD_TYPE=Release    # Release build
```

### Autotools Options
```bash
./configure \
  --enable-asan                 # Memory debugging
  --enable-static-tools         # Static executables
  --enable-portable-target      # No SIMD optimizations
  --with-libclang               # Reflection support
```

## Common Scenarios

### Quick Iteration During Development
```bash
# Fastest: compile and run one test
make && ./test/libfyaml-test

# Or with CMake
cmake --build build && ./build/test/libfyaml-test
```

### Before Committing
```bash
# Run full local test suite
./test-local.sh

# Optional: validate workflows
act -W .github/workflows/ci.yaml -n
act -W .github/workflows/cmake.yaml -n
```

### After Committing (GitHub CI)
- Push to branch
- GitHub runs all workflows automatically
- Check Actions tab for results

### Memory Leak Testing
```bash
# With valgrind
./test-local.sh --valgrind

# With ASAN (faster)
cmake -B build -DENABLE_ASAN=ON && cmake --build build && cd build && ctest
```

### Cross-Platform Testing
1. Test locally on your platform: `./test-local.sh`
2. Push to GitHub: full matrix testing on Linux/macOS/Windows/BSD
3. Check self-hosted runners for BSD results

## File Reference

| File | Purpose |
|------|---------|
| `test-local.sh` | Run CI tests locally |
| `install-deps.sh` | Install build dependencies |
| `demo-act.sh` | Demonstrate act usage |
| `LOCAL-TESTING.md` | Detailed local testing guide |
| `BSD-SETUP.md` | BSD-specific instructions |
| `RUN-WORKFLOWS-LOCALLY.md` | Complete act documentation |
| `TESTING-QUICK-REFERENCE.md` | This file |

## Troubleshooting

### Tests failing locally
```bash
# Clean rebuild
rm -rf build
./test-local.sh --cmake-only
```

### act not working
```bash
# Check Docker is running
docker ps

# Use native local testing instead
./test-local.sh
```

### BSD build issues
```bash
# Use gmake, not make
gmake clean && gmake

# NetBSD: Use CMake for testing
cd build && ctest
```

## Getting Help

- **Build issues:** Check `LOCAL-TESTING.md`
- **BSD issues:** Check `BSD-SETUP.md`
- **Workflow issues:** Check `RUN-WORKFLOWS-LOCALLY.md`
- **GitHub Actions:** https://docs.github.com/en/actions
- **act tool:** https://github.com/nektos/act
