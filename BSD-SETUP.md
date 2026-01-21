# BSD Build and Testing Guide

This guide covers building and testing libfyaml on FreeBSD, OpenBSD, and NetBSD.

## FreeBSD

### Installing Dependencies

```bash
# Install build dependencies
sudo pkg install -y \
  autoconf automake libtool gmake \
  libyaml pkgconf check \
  python3 py39-pip py39-setuptools \
  cmake ninja gcc
```

### Building with Autotools

```bash
./bootstrap.sh
./configure
gmake -j$(sysctl -n hw.ncpu)
gmake check
gmake distcheck
```

### Building with CMake

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DENABLE_NETWORK=ON

cmake --build build
cd build && ctest --output-on-failure
```

### Compiler Selection

```bash
# Use GCC
export CC=gcc
export CXX=g++

# Use Clang (default)
export CC=clang
export CXX=clang++
```

## OpenBSD

### Installing Dependencies

```bash
# Install build dependencies
sudo pkg_add -I \
  autoconf-2.71 automake-1.16.5 libtool gmake \
  libyaml pkgconf check \
  python3 py3-pip py3-setuptools \
  cmake ninja gcc
```

### Building with Autotools

```bash
./bootstrap.sh
./configure
gmake -j$(sysctl -n hw.ncpu)
gmake check
gmake distcheck
```

### Building with CMake

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DENABLE_NETWORK=ON

cmake --build build
cd build && ctest --output-on-failure
```

### Compiler Selection

```bash
# Use GCC (installed as egcc/eg++)
export CC=egcc
export CXX=eg++

# Use Clang (default)
export CC=clang
export CXX=clang++
```

## NetBSD

### Installing Dependencies

```bash
# Install build dependencies
sudo pkgin -y install \
  autoconf automake libtool gmake \
  libyaml pkg-config check \
  python3 py39-pip py39-setuptools \
  cmake ninja-build gcc12
```

### Building with Autotools

```bash
./bootstrap.sh
./configure
gmake -j$(sysctl -n hw.ncpu)

# Note: make check is currently not working on NetBSD with Autotools
# gmake check

gmake distcheck
```

### Building with CMake

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DENABLE_NETWORK=ON

cmake --build build
cd build && ctest --output-on-failure
```

### Known Issues

- **Autotools `make check` fails**: The Autotools test suite currently has issues on NetBSD. Use CMake for testing, or run the test binaries directly.

### Compiler Selection

```bash
# Use GCC
export CC=gcc
export CXX=g++

# Use Clang (default)
export CC=clang
export CXX=clang++
```

## Common Notes for All BSDs

### Using GNU Make

All BSDs require GNU Make (`gmake`) for building. The system `make` is BSD make and won't work.

```bash
# Always use gmake, not make
gmake
gmake check
gmake distcheck
```

### CPU Count

Get the number of CPUs for parallel builds:

```bash
sysctl -n hw.ncpu
```

### Testing Individual Components

```bash
# Run test binary directly
./test/libfyaml-test

# Run specific test suite
./test/libfyaml.test
./test/testerrors.test
./test/testemitter.test
```

## Setting Up Self-Hosted GitHub Runners for BSDs

The GitHub Actions workflows for FreeBSD, OpenBSD, and NetBSD require self-hosted runners. Here's how to set them up:

### 1. Install GitHub Actions Runner

On each BSD system:

```bash
# Create a directory for the runner
mkdir actions-runner && cd actions-runner

# Download the latest runner package
# Visit: https://github.com/<owner>/<repo>/settings/actions/runners/new
# Copy the download and config commands provided by GitHub

# For example (check GitHub for current version):
curl -o actions-runner-bsd-x64-2.311.0.tar.gz -L \
  https://github.com/actions/runner/releases/download/v2.311.0/actions-runner-bsd-x64-2.311.0.tar.gz

# Extract the installer
tar xzf ./actions-runner-bsd-x64-2.311.0.tar.gz

# Configure the runner
./config.sh --url https://github.com/<owner>/<repo> --token <TOKEN>
```

### 2. Add Runner Labels

When configuring the runner, add appropriate labels:

**FreeBSD:**
```bash
./config.sh --url https://github.com/<owner>/<repo> \
  --token <TOKEN> \
  --labels self-hosted,freebsd
```

**OpenBSD:**
```bash
./config.sh --url https://github.com/<owner>/<repo> \
  --token <TOKEN> \
  --labels self-hosted,openbsd
```

**NetBSD:**
```bash
./config.sh --url https://github.com/<owner>/<repo> \
  --token <TOKEN> \
  --labels self-hosted,netbsd
```

### 3. Run the Runner

```bash
# Run interactively for testing
./run.sh

# Install as a service (FreeBSD)
sudo ./svc.sh install
sudo ./svc.sh start

# Install as a service (OpenBSD)
# Create /etc/rc.d/actions_runner script

# Install as a service (NetBSD)
# Create /etc/rc.d/actions_runner script
```

### 4. Install Required Dependencies

Make sure all build dependencies are installed on each runner (see platform-specific sections above).

### 5. Verify Runner

After setup, the runner should appear in:
```
https://github.com/<owner>/<repo>/settings/actions/runners
```

The status should show "Idle" (ready) or "Active" (running a job).

## Troubleshooting

### Autotools Bootstrap Fails

```bash
# Make sure autotools are in PATH
which autoconf automake libtool

# Try with explicit ACLOCAL_PATH
export ACLOCAL_PATH=/usr/local/share/aclocal
./bootstrap.sh
```

### CMake Can't Find Dependencies

```bash
# Set PKG_CONFIG_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

# Or specify paths explicitly
cmake -B build \
  -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_BUILD_TYPE=Release
```

### Test Failures

```bash
# Get verbose output
gmake check VERBOSE=1  # Autotools
cd build && ctest -V    # CMake

# Check test logs
cat test/*.log
cat test-suite.log
cat build/Testing/Temporary/LastTest.log
```

### Compiler Not Found

```bash
# Check available compilers
which gcc clang egcc
cc --version

# Install additional compiler if needed (FreeBSD)
sudo pkg install gcc

# Install additional compiler if needed (OpenBSD)
sudo pkg_add gcc

# Install additional compiler if needed (NetBSD)
sudo pkgin install gcc12
```

## Performance Notes

BSD systems often have different performance characteristics than Linux:

- **Memory allocators**: BSD malloc implementations differ from glibc
- **SIMD support**: Check CPU flags with `sysctl hw.model`
- **Threading**: Different pthread implementations may affect performance

For optimal performance, test with both GCC and Clang compilers.

## Contributing BSD Fixes

If you encounter issues on BSD systems:

1. Test with both Autotools and CMake
2. Test with both GCC and Clang
3. Check if the issue is BSD-specific
4. Report with full system information:
   ```bash
   uname -a
   cc --version
   cmake --version
   gmake --version
   ```

## Additional Resources

- FreeBSD Ports: https://www.freebsd.org/ports/
- OpenBSD Packages: https://openports.pl/
- NetBSD pkgsrc: https://www.pkgsrc.org/
- GitHub Actions self-hosted runners: https://docs.github.com/en/actions/hosting-your-own-runners
