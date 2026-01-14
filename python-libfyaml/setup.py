#!/usr/bin/env python3
"""
Setup script for libfyaml Python bindings.

This creates a C extension module that wraps libfyaml's exported generic API
to provide a NumPy-like interface for YAML/JSON parsing.

Build modes (in priority order):
1. Local build: Use ../build/ directory (default if exists)
2. System install: Use pkg-config to find installed libfyaml
3. Auto-build: Build libfyaml locally if LIBFYAML_BUILD_LOCAL=1

Environment variables:
- LIBFYAML_BUILD_LOCAL=1  : Force use of local build (build if needed)
- LIBFYAML_REBUILD=1      : Rebuild libfyaml before installing
- LIBFYAML_USE_SYSTEM=1   : Force use of system-installed libfyaml
"""

import os
import sys
import subprocess
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

def run_command(cmd, cwd=None):
    """Run a shell command and return success status."""
    try:
        subprocess.run(cmd, shell=True, check=True, cwd=cwd)
        return True
    except subprocess.CalledProcessError:
        return False

def get_pkg_config(package, option):
    """Get pkg-config information."""
    try:
        result = subprocess.run(
            ['pkg-config', option, package],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip().split()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

def build_local_libfyaml(parent_dir):
    """Build libfyaml locally using CMake."""
    build_dir = os.path.join(parent_dir, 'build')

    print("=" * 70)
    print("Building libfyaml locally...")
    print("=" * 70)

    # Create build directory
    os.makedirs(build_dir, exist_ok=True)

    # Run cmake
    print("Running CMake configuration...")
    if not run_command('cmake ..', cwd=build_dir):
        print("ERROR: CMake configuration failed")
        return False

    # Build
    print("Building libfyaml...")
    if not run_command('cmake --build . --target fyaml', cwd=build_dir):
        print("ERROR: Build failed")
        return False

    print("✓ Local build successful")
    return True

class CustomBuildExt(build_ext):
    """Custom build extension to handle libfyaml building."""

    def run(self):
        parent_dir = os.path.abspath('..')
        build_dir = os.path.join(parent_dir, 'build')

        # Check if rebuild requested
        if os.environ.get('LIBFYAML_REBUILD') == '1':
            print("LIBFYAML_REBUILD=1: Rebuilding libfyaml...")
            build_local_libfyaml(parent_dir)

        # Check if local build forced
        elif os.environ.get('LIBFYAML_BUILD_LOCAL') == '1':
            if not os.path.exists(os.path.join(build_dir, 'libfyaml.so')):
                print("LIBFYAML_BUILD_LOCAL=1: Building libfyaml...")
                build_local_libfyaml(parent_dir)

        # Run the actual build
        build_ext.run(self)

def configure_extension():
    """Configure the extension based on available libfyaml installation."""

    parent_dir = os.path.abspath('..')
    local_build = os.path.join(parent_dir, 'build')

    include_dirs = []
    library_dirs = []
    libraries = ['fyaml']
    runtime_library_dirs = []
    extra_compile_args = ['-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter']

    # Determine which libfyaml to use
    use_system = os.environ.get('LIBFYAML_USE_SYSTEM') == '1'
    use_local = os.environ.get('LIBFYAML_BUILD_LOCAL') == '1'

    if use_system:
        # Force system installation
        print("=" * 70)
        print("Using system-installed libfyaml (LIBFYAML_USE_SYSTEM=1)")
        print("=" * 70)

        # Try pkg-config
        cflags = get_pkg_config('libfyaml', '--cflags-only-I')
        ldflags = get_pkg_config('libfyaml', '--libs-only-L')

        if cflags or ldflags:
            include_dirs = [flag[2:] for flag in cflags]
            library_dirs = [flag[2:] for flag in ldflags]
            print(f"✓ Found via pkg-config")
        else:
            # Fall back to standard paths
            print("⚠ pkg-config not found, using standard paths")
            include_dirs = ['/usr/local/include', '/usr/include']
            library_dirs = ['/usr/local/lib', '/usr/lib']

    elif os.path.exists(local_build) or use_local:
        # Use local build
        print("=" * 70)
        print("Using local libfyaml build")
        print("=" * 70)

        include_dirs = [
            os.path.join(parent_dir, 'include'),
            os.path.join(parent_dir, 'build'),
        ]
        library_dirs = [local_build]
        runtime_library_dirs = [local_build]  # Add RPATH for local testing

        # Check if build exists
        if os.path.exists(os.path.join(local_build, 'libfyaml.so')):
            print(f"✓ Using existing build: {local_build}")
        else:
            print(f"⚠ Build directory exists but libfyaml.so not found")
            print(f"  Run: LIBFYAML_BUILD_LOCAL=1 pip install .")

    else:
        # Try system installation via pkg-config
        print("=" * 70)
        print("Searching for libfyaml...")
        print("=" * 70)

        cflags = get_pkg_config('libfyaml', '--cflags-only-I')
        ldflags = get_pkg_config('libfyaml', '--libs-only-L')

        if cflags or ldflags:
            include_dirs = [flag[2:] for flag in cflags]
            library_dirs = [flag[2:] for flag in ldflags]
            print(f"✓ Found system libfyaml via pkg-config")
        else:
            print("⚠ libfyaml not found!")
            print("")
            print("Options:")
            print("  1. Install system-wide: cd .. && mkdir build && cd build && cmake .. && sudo make install")
            print("  2. Use local build:     LIBFYAML_BUILD_LOCAL=1 pip install .")
            print("  3. Build from parent:   cd ../build && cmake .. && cmake --build .")
            print("")
            # Set default paths and hope for the best
            include_dirs = ['/usr/local/include', '/usr/include']
            library_dirs = ['/usr/local/lib', '/usr/lib']

    print(f"Include directories: {include_dirs}")
    print(f"Library directories: {library_dirs}")
    print(f"Libraries: {libraries}")
    if runtime_library_dirs:
        print(f"Runtime library dirs (RPATH): {runtime_library_dirs}")
    print("=" * 70)

    return Extension(
        'libfyaml._libfyaml',
        sources=['libfyaml/_libfyaml.c'],
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=libraries,
        runtime_library_dirs=runtime_library_dirs,
        extra_compile_args=extra_compile_args,
    )

# Configure the extension
ext_module = configure_extension()

setup(
    ext_modules=[ext_module],
    packages=['libfyaml'],
    cmdclass={'build_ext': CustomBuildExt},
)
