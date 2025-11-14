# Changelog

All notable changes to the libfyaml Python bindings will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.0] - 2025-01-XX

### Added
- **PyPI-ready packaging** with modern pyproject.toml
- **Flexible build system** supporting local and system-installed libfyaml
- **Environment variable controls** for build configuration:
  - `LIBFYAML_BUILD_LOCAL=1` - Use local build (auto-build if needed)
  - `LIBFYAML_REBUILD=1` - Rebuild libfyaml before installing
  - `LIBFYAML_USE_SYSTEM=1` - Force use of system-installed libfyaml
- **RPATH support** for local builds (no LD_LIBRARY_PATH needed)
- **Comprehensive test suite** with 34 tests covering all core functionality
- **Multi-document YAML support**:
  - `loads_all()` - Parse multiple YAML documents from string
  - `load_all()` - Parse multiple YAML documents from file
  - `dumps_all()` - Emit multiple YAML documents to string
  - `dump_all()` - Emit multiple YAML documents to file
- **MIT License** file
- **MANIFEST.in** for proper source distributions
- **Installation documentation** with multiple build modes
- **Automated testing** infrastructure for CI/CD

### Changed
- Migrated from internal static libraries to exported generic API
- Updated setup.py with intelligent libfyaml detection (local vs system)
- Improved build system to use CMake only (dropped autotools support)
- Enhanced README with comprehensive installation instructions
- Version bumped to 0.9.0 (approaching 1.0 stable release)

### Fixed
- Memory management using `fy_gb_trim()` instead of direct struct access
- All required symbols properly exported from shared library
- Build process now works cleanly without LD_LIBRARY_PATH

## [0.2.0] - 2024-XX-XX

### Added
- Zero-casting NumPy-like interface
- Automatic type conversion for arithmetic and comparisons
- String method delegation (upper(), lower(), startswith(), etc.)
- Dict-like interface (keys(), values(), items(), get())
- List-like interface with indexing and iteration
- `to_python()` method for full conversion to native Python objects
- `copy()` method (alias for to_python())
- `from_python()` function for Python→FyGeneric conversion
- Deduplication support with `dedup=True` parameter
- Auto-trim memory management with `trim=True` parameter
- File I/O helpers: `load()` and `dump()`
- JSON mode support with `mode='json'` parameter

### Performance
- 22x faster than PyYAML on typical workloads
- 7.7x less memory than PyYAML
- Production-ready memory efficiency for large files
- Inline storage for small values (zero allocations)

## [0.1.0] - 2024-XX-XX

### Added
- Initial proof-of-concept release
- Basic YAML parsing with libfyaml backend
- FyGeneric wrapper type
- Simple loads()/dumps() interface
- Python 3.7+ support

---

## Upcoming in 1.0.0

### Planned Features
- Windows support
- Type stubs for mypy/pylint
- Comprehensive performance benchmarks
- CI/CD with GitHub Actions
- PyPI publication
- Documentation site
- Mutable document modification support
- Enhanced error messages with line/column information

### API Stability
Version 1.0.0 will mark the stable API. All breaking changes will be made before 1.0.

---

## Version Numbering

- **0.9.x** - Release candidates for 1.0 (PyPI-ready, production-quality)
- **1.0.0** - First stable release with API guarantees
- **1.x.y** - Stable releases (no breaking changes)
- **2.0.0** - Next major version (if breaking changes needed)
