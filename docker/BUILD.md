# Quick Start: Building and Running

## Build Images

```bash
cd docker

# Build all images
docker build -f Dockerfile.ubuntu -t libfyaml-ci:ubuntu .
docker build -f Dockerfile.debian -t libfyaml-ci:debian .
docker build -f Dockerfile.macos-like -t libfyaml-ci:macos-like .
```

## Run Builds

```bash
# CMake build (Ubuntu)
docker run --rm -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu \
    ./docker/scripts/build-cmake.sh

# CMake build (Debian)
docker run --rm -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:debian \
    ./docker/scripts/build-cmake.sh

# CMake build (macOS-like with Clang)
docker run --rm -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:macos-like \
    ./docker/scripts/build-cmake.sh

# Autotools build
docker run --rm -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu \
    ./docker/scripts/build-autotools.sh

# CMake with AddressSanitizer
docker run --rm -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu \
    ./docker/scripts/build-cmake-asan.sh
```

## Interactive Shell

```bash
docker run --rm -it -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu
docker run --rm -it -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:debian
docker run --rm -it -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:macos-like
```

## Use Different Compiler

```bash
# Ubuntu with Clang instead of GCC
docker run --rm -e CC=clang -e CXX=clang++ \
    -v $(pwd)/..:/home/builder/libfyaml:ro libfyaml-ci:ubuntu \
    ./docker/scripts/build-cmake.sh
```

## Cleanup

```bash
docker rmi libfyaml-ci:ubuntu libfyaml-ci:debian libfyaml-ci:macos-like
```
