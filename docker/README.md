# Docker Build Environments for libfyaml

This directory contains Docker configurations for building and testing libfyaml in isolated, reproducible environments.

## Available Environments

| Dockerfile | Base Image | Default Compiler | Description |
|------------|------------|------------------|-------------|
| `Dockerfile.ubuntu` | Ubuntu 24.04 | GCC | Mirrors GitHub Actions `ubuntu-latest` |
| `Dockerfile.debian` | Debian Bookworm | GCC | Stable Debian build environment |
| `Dockerfile.macos-like` | Ubuntu 24.04 | Clang | Linux env configured like macOS |

> **Note:** Docker cannot run actual macOS. The `macos-like` environment provides a Linux container with Clang as the default compiler, similar to macOS. For real macOS testing, use GitHub Actions or a macOS machine.

## Quick Start

### Using Docker Compose (Recommended)

```bash
cd docker

# Build all images
docker compose build

# Run interactive shell
docker compose run ubuntu
docker compose run debian
docker compose run macos-like

# Run with different compiler
docker compose run ubuntu-clang
docker compose run debian-clang
```

### Using Docker Directly

```bash
cd docker

# Build an image
docker build -f Dockerfile.ubuntu -t libfyaml-ci:ubuntu .

# Run interactive shell
docker run -it -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu

# Run with specific compiler
docker run -it -e CC=clang -e CXX=clang++ \
    -v $(pwd)/..:/home/builder/libfyaml:ro \
    libfyaml-ci:ubuntu
```

## Build Scripts

Helper scripts are provided in `scripts/`:

| Script | Description |
|--------|-------------|
| `build-autotools.sh` | Build with Autotools (bootstrap, configure, make) |
| `build-cmake.sh` | Build with CMake + Ninja |
| `build-cmake-asan.sh` | Build with CMake + AddressSanitizer |

### Running Build Scripts

```bash
# Autotools build
docker compose run ubuntu ./docker/scripts/build-autotools.sh

# CMake Release build
docker compose run debian ./docker/scripts/build-cmake.sh

# CMake Debug build
docker compose run -e BUILD_TYPE=Debug ubuntu ./docker/scripts/build-cmake.sh

# CMake with ASAN
docker compose run ubuntu ./docker/scripts/build-cmake-asan.sh

# Using Clang
docker compose run ubuntu-clang ./docker/scripts/build-cmake.sh
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CC` | `gcc` or `clang` | C compiler |
| `CXX` | `g++` or `clang++` | C++ compiler |
| `BUILD_TYPE` | `Release` | CMake build type (Debug/Release) |
| `BUILD_DIR` | `/home/builder/build/*` | Build output directory |

## Volume Mounts

The docker-compose configuration mounts:

- **Source code** (`..`) as read-only at `/home/builder/libfyaml`
- **Build directory** as a named volume at `/home/builder/build`

This keeps build artifacts separate from source and allows for incremental builds.

## Manual Build Commands

If you prefer to run commands manually inside the container:

### Autotools

```bash
docker compose run ubuntu bash
# Inside container:
cd /home/builder
cp -r libfyaml build-autotools
cd build-autotools
./bootstrap.sh
./configure
make -j$(nproc)
make check
```

### CMake

```bash
docker compose run ubuntu bash
# Inside container:
mkdir -p /home/builder/build && cd /home/builder/build
cmake /home/builder/libfyaml -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build .
ctest --output-on-failure
```

## Testing Different Configurations

### Test Matrix

```bash
# Ubuntu + GCC
docker compose run ubuntu ./docker/scripts/build-cmake.sh

# Ubuntu + Clang
docker compose run ubuntu-clang ./docker/scripts/build-cmake.sh

# Debian + GCC
docker compose run debian ./docker/scripts/build-cmake.sh

# Debian + Clang
docker compose run debian-clang ./docker/scripts/build-cmake.sh

# macOS-like (Clang)
docker compose run macos-like ./docker/scripts/build-cmake.sh
```

### Debug vs Release

```bash
# Debug build
docker compose run -e BUILD_TYPE=Debug ubuntu ./docker/scripts/build-cmake.sh

# Release build (default)
docker compose run -e BUILD_TYPE=Release ubuntu ./docker/scripts/build-cmake.sh
```

## Cleaning Up

```bash
# Remove containers
docker compose down

# Remove containers and volumes (clears build cache)
docker compose down -v

# Remove images
docker compose down --rmi all

# Full cleanup
docker compose down -v --rmi all
```

## Comparison with GitHub Actions

| Feature | Docker | GitHub Actions |
|---------|--------|----------------|
| Ubuntu testing | Full support | Full support |
| Debian testing | Full support | Not available |
| macOS testing | Emulated (Linux) | Full support |
| Windows testing | Not available | Full support |
| Speed | Fast (local) | Slower (remote) |
| Cost | Free | Free (public repos) |
| Reproducibility | Excellent | Good |

## Troubleshooting

### Permission denied on build scripts

```bash
chmod +x docker/scripts/*.sh
```

### Out of disk space

```bash
# Clean up Docker resources
docker system prune -a
docker volume prune
```

### Build fails with "read-only file system"

The source is mounted read-only. Build scripts copy source to a writable location. For manual builds, copy source first:

```bash
cp -r /home/builder/libfyaml /home/builder/build-src
cd /home/builder/build-src
# Now you can build
```

### LLVM/reflection features not working

Ensure LLVM packages are installed. Check with:

```bash
docker compose run ubuntu bash -c "llvm-config --version"
```
