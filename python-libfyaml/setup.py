#!/usr/bin/env python3
"""Build configuration for the libfyaml Python extension."""

import os
import re
import struct
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


THIS_DIR = Path(__file__).resolve().parent


def resolve_repo_root() -> Path:
    env_root = os.environ.get("LIBFYAML_REPO_ROOT")
    candidates = []

    if env_root:
        candidates.append(Path(env_root))

    candidates.append(THIS_DIR.parent)

    for candidate in candidates:
        resolved = candidate.resolve()
        if (resolved / "CMakeLists.txt").exists() and (resolved / "include" / "libfyaml.h").exists():
            return resolved

    return candidates[0].resolve() if candidates else THIS_DIR.parent.resolve()


REPO_ROOT = resolve_repo_root()


def _pep440_from_libfyaml_version(raw_version: str) -> str:
    """Translate libfyaml release versions to PEP 440."""
    match = re.fullmatch(r"(\d+\.\d+\.\d+)(?:-(alpha|beta|rc)(\d+))?", raw_version.strip())
    if not match:
        raise RuntimeError(f"Unsupported libfyaml release version format: {raw_version!r}")

    version = match.group(1)
    if match.group(2):
        version += {"alpha": "a", "beta": "b", "rc": "rc"}[match.group(2)] + match.group(3)
    return version


def _read_pkg_info_version() -> Optional[str]:
    """Read the version from sdist metadata when repo version files are absent."""
    for candidate in (THIS_DIR / "PKG-INFO", THIS_DIR / "libfyaml.egg-info" / "PKG-INFO"):
        if not candidate.exists():
            continue
        for line in candidate.read_text().splitlines():
            if line.startswith("Version: "):
                return line.split(": ", 1)[1].strip()
    return None


def resolve_package_version() -> str:
    """Resolve the Python package version from the core libfyaml release version."""
    version_file = REPO_ROOT / ".tarball-version"
    if version_file.exists():
        return _pep440_from_libfyaml_version(version_file.read_text().strip())

    git_version_gen = REPO_ROOT / "build-aux" / "git-version-gen"
    if git_version_gen.exists():
        result = subprocess.run(
            [str(git_version_gen), str(version_file)],
            capture_output=True,
            text=True,
            check=True,
        )
        return _pep440_from_libfyaml_version(result.stdout.strip())

    pkg_info_version = _read_pkg_info_version()
    if pkg_info_version:
        return pkg_info_version

    raise RuntimeError("Could not determine libfyaml package version")


def run_command(args: List[str], cwd: Optional[Path] = None) -> None:
    """Run a command and fail loudly on error."""
    subprocess.run(args, cwd=cwd, check=True)


def get_pkg_config(package: str, option: str) -> List[str]:
    """Return pkg-config output split into arguments."""
    try:
        result = subprocess.run(
            ["pkg-config", option, package],
            capture_output=True,
            text=True,
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    return result.stdout.strip().split()


def have_repo_sources() -> bool:
    """Check whether the full libfyaml source tree is available."""
    return (REPO_ROOT / "CMakeLists.txt").exists() and (
        REPO_ROOT / "include" / "libfyaml.h"
    ).exists()


def generic_platform_supported() -> bool:
    """Return True when libfyaml generics are supported on this target."""
    return struct.calcsize("P") == 8 and sys.byteorder == "little"


def base_compile_args(compiler_type: Optional[str]) -> List[str]:
    if compiler_type == "msvc":
        return ["/W3", "/wd4100"]

    args = ["-Wall", "-Wextra", "-Wno-unused-parameter"]
    if sys.platform != "win32":
        args.insert(0, "-std=gnu2x")
    return args


def base_link_args() -> Tuple[List[str], List[str]]:
    extra_link_args: List[str] = []
    libraries: List[str] = []

    if sys.platform.startswith("linux"):
        extra_link_args.append("-pthread")
        libraries.append("m")

    return extra_link_args, libraries


def windows_compiler_supported(compiler_type: Optional[str], compiler) -> bool:
    if sys.platform != "win32":
        return True

    candidates = []
    for env_name in ("CC", "CXX"):
        value = os.environ.get(env_name)
        if value:
            candidates.extend(shlex.split(value))

    for attr in ("compiler", "compiler_so", "linker_so"):
        value = getattr(compiler, attr, None)
        if isinstance(value, (list, tuple)):
            candidates.extend(str(item) for item in value)
        elif value:
            candidates.append(str(value))

    if compiler_type == "unix":
        return True

    normalized = " ".join(candidates).lower()
    return "clang" in normalized


class CustomBuildExt(build_ext):
    """Build the extension against a bundled static libfyaml when possible."""

    def build_extension(self, ext: Extension) -> None:
        if not generic_platform_supported():
            raise RuntimeError(
                "libfyaml generics are currently supported only on 64-bit "
                "little-endian targets"
            )

        compiler_type = getattr(self.compiler, "compiler_type", None)
        if not windows_compiler_supported(compiler_type, self.compiler):
            raise RuntimeError(
                "Windows Python bindings require a Clang-family compiler "
                "(clang or clang-cl)."
            )
        build_info = self._resolve_libfyaml_build(compiler_type)

        ext.include_dirs = build_info["include_dirs"]
        ext.library_dirs = build_info["library_dirs"]
        ext.runtime_library_dirs = build_info["runtime_library_dirs"]
        ext.libraries = build_info["libraries"]
        ext.extra_objects = build_info["extra_objects"]
        ext.extra_compile_args = build_info["extra_compile_args"]
        ext.extra_link_args = build_info["extra_link_args"]

        super().build_extension(ext)

    def _resolve_libfyaml_build(self, compiler_type: Optional[str]) -> Dict[str, List[str]]:
        if os.environ.get("LIBFYAML_USE_SYSTEM") == "1":
            return self._system_build_info(compiler_type)

        if have_repo_sources():
            return self._bundled_build_info(compiler_type)

        return self._system_build_info(compiler_type)

    def _bundled_build_info(self, compiler_type: Optional[str]) -> Dict[str, List[str]]:
        build_dir = Path(self.build_temp) / "libfyaml-bundled"
        if build_dir.exists():
            shutil.rmtree(build_dir)
        cmake_args = [
            "cmake",
            "-S",
            str(REPO_ROOT),
            "-B",
            str(build_dir),
            "-DBUILD_SHARED_LIBS=OFF",
            "-DBUILD_TESTING=OFF",
            "-DENABLE_NETWORK=OFF",
            "-DENABLE_PYTHON_BINDINGS=OFF",
            "-DENABLE_REFLECTION=OFF",
            "-DENABLE_LIBCLANG=OFF",
            "-DENABLE_PORTABLE_TARGET=ON",
            "-DCMAKE_BUILD_TYPE=Release",
        ]

        if "CMAKE_GENERATOR" not in os.environ and shutil.which("ninja"):
            cmake_args.extend(["-G", "Ninja"])

        extra_cmake_args = os.environ.get("LIBFYAML_CMAKE_ARGS")
        if extra_cmake_args:
            cmake_args.extend(shlex.split(extra_cmake_args))

        run_command(cmake_args)
        run_command(
            ["cmake", "--build", str(build_dir), "--config", "Release", "--target", "fyaml_static"]
        )

        static_library = build_dir / (
            "fyaml_static.lib" if sys.platform == "win32" else "libfyaml_static.a"
        )
        if not static_library.exists():
            raise RuntimeError(f"Bundled static library was not produced: {static_library}")

        extra_link_args, libraries = base_link_args()
        return {
            "include_dirs": [str(REPO_ROOT / "include"), str(build_dir)],
            "library_dirs": [],
            "runtime_library_dirs": [],
            "libraries": libraries,
            "extra_objects": [str(static_library)],
            "extra_compile_args": base_compile_args(compiler_type),
            "extra_link_args": extra_link_args,
        }

    def _system_build_info(self, compiler_type: Optional[str]) -> Dict[str, List[str]]:
        include_dirs: List[str] = []
        library_dirs: List[str] = []
        runtime_library_dirs: List[str] = []
        libraries = ["fyaml"]

        cflags = get_pkg_config("libfyaml", "--cflags-only-I")
        ldflags = get_pkg_config("libfyaml", "--libs-only-L")
        libs = get_pkg_config("libfyaml", "--libs-only-l")

        include_env = os.environ.get("LIBFYAML_INCLUDE_DIR")
        library_env = os.environ.get("LIBFYAML_LIBRARY_DIR")
        libraries_env = os.environ.get("LIBFYAML_LIBRARIES")

        if cflags or ldflags or libs:
            include_dirs = [flag[2:] for flag in cflags]
            library_dirs = [flag[2:] for flag in ldflags]
            libraries = [flag[2:] for flag in libs] or libraries
        elif include_env or library_env or libraries_env:
            if include_env:
                include_dirs = [path for path in include_env.split(os.pathsep) if path]
            if library_env:
                library_dirs = [path for path in library_env.split(os.pathsep) if path]
            if libraries_env:
                libraries = [lib for lib in libraries_env.split(os.pathsep) if lib]
        else:
            if sys.platform == "win32":
                raise RuntimeError(
                    "System libfyaml lookup on Windows requires pkg-config or "
                    "LIBFYAML_INCLUDE_DIR/LIBFYAML_LIBRARY_DIR."
                )
            include_dirs = ["/usr/local/include", "/usr/include"]
            library_dirs = ["/usr/local/lib", "/usr/lib"]

        extra_link_args, extra_libraries = base_link_args()
        for library in extra_libraries:
            if library not in libraries:
                libraries.append(library)

        return {
            "include_dirs": include_dirs,
            "library_dirs": library_dirs,
            "runtime_library_dirs": runtime_library_dirs,
            "libraries": libraries,
            "extra_objects": [],
            "extra_compile_args": base_compile_args(compiler_type),
            "extra_link_args": extra_link_args,
        }


setup(
    version=resolve_package_version(),
    ext_modules=[Extension("libfyaml._libfyaml", sources=["libfyaml/_libfyaml.c"])],
    packages=["libfyaml"],
    package_data={"libfyaml": ["*.pyi"]},
    cmdclass={"build_ext": CustomBuildExt},
)
