#!/usr/bin/env python3
"""
Setup script for libfyaml Python bindings.

This creates a C extension module that wraps libfyaml's generic type system
to provide a NumPy-like interface for YAML/JSON parsing.
"""

import os
import sys
from setuptools import setup, Extension
import subprocess

# Build against internal static library and headers
parent_dir = os.path.abspath('..')

# Include internal headers for generic API
include_dirs = [
    os.path.join(parent_dir, 'include'),          # Public headers
    os.path.join(parent_dir, 'src'),              # Internal headers
    os.path.join(parent_dir, 'src', 'lib'),       # Library internals
    os.path.join(parent_dir, 'src', 'generic'),   # Generic API
    os.path.join(parent_dir, 'src', 'allocator'), # Allocator API
    os.path.join(parent_dir, 'src', 'util'),      # Utilities
    os.path.join(parent_dir, 'src', 'thread'),    # Thread utilities
    os.path.join(parent_dir, 'build'),            # Generated config.h
]

# Link against static library
library_dirs = []
libraries = []

# Use CMake build directory if available
cmake_build = os.path.join(parent_dir, 'build')
autotools_build = os.path.join(parent_dir, 'src', 'lib', '.libs')

if os.path.exists(cmake_build):
    library_dirs.append(cmake_build)
    print(f"Using CMake build: {cmake_build}")
elif os.path.exists(autotools_build):
    library_dirs.append(autotools_build)
    print(f"Using Autotools build: {autotools_build}")
else:
    print("WARNING: No build directory found!")

# Link shared library (it includes the generic API internally)
libraries = ['fyaml']

print(f"Include directories: {include_dirs}")
print(f"Library directories: {library_dirs}")
print(f"Libraries: {libraries}")

# Link against static library to get internal generic API
static_lib = os.path.join(cmake_build, 'libfyaml_static.a')

# Get LLVM libs (static library includes reflection which needs LLVM)
import subprocess
try:
    llvm_libs_output = subprocess.check_output(['llvm-config-15', '--libs', 'all'], stderr=subprocess.STDOUT)
    llvm_libs = llvm_libs_output.decode().strip().split()
    llvm_ldflags = subprocess.check_output(['llvm-config-15', '--ldflags'], stderr=subprocess.STDOUT)
    llvm_ldflags = llvm_ldflags.decode().strip().split()
    print(f"LLVM libs: {llvm_libs[:5]}...")  # Show first few
    print(f"LLVM ldflags: {llvm_ldflags}")
except:
    print("WARNING: llvm-config not found, LLVM linking may fail")
    llvm_libs = []
    llvm_ldflags = []

# Define the extension module - link against static library
ext_module = Extension(
    'libfyaml._libfyaml',
    sources=['libfyaml/_libfyaml_minimal.c'],
    include_dirs=include_dirs,
    extra_compile_args=['-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter'],
    extra_objects=[static_lib],  # Link the static library directly
    extra_link_args=llvm_ldflags + llvm_libs,  # Link LLVM
)

setup(
    ext_modules=[ext_module],
    packages=['libfyaml'],
)
