# Installing act - Safe Methods

This guide shows how to safely install `act` without using `curl | sudo bash`.

## Why Not Use curl | sudo bash?

The common installation method:
```bash
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash
```

...is convenient but has security risks:
- Pipes untrusted code directly to bash with sudo privileges
- No chance to review what will be executed
- Could be compromised if GitHub or the repository is compromised

## Package Manager Status

**Unfortunately, act is NOT available in:**
- ✗ Official Debian/Ubuntu repositories
- ✗ Official Debian/Ubuntu PPAs
- ✗ Snap Store (there's a different "act" program in snap)
- ✗ Flatpak

**act IS available in:**
- ✓ Homebrew (macOS and Linux)
- ✓ GitHub Releases (official binaries)
- ✓ AUR (Arch Linux)

## Recommended Installation Methods

### Method 1: Safe Installation Script (Easiest)

We provide a safe installation script that downloads, verifies, and prompts before installing:

```bash
./install-act-safe.sh
```

**What it does:**
1. Fetches the latest version from GitHub API
2. Downloads official binary from GitHub releases
3. Extracts and verifies the binary
4. **Asks permission** before installing
5. Offers user-local installation (no sudo needed)

**No automatic sudo execution!**

### Method 2: Manual Installation (Most Secure)

Complete control over every step:

#### Step 1: Download

```bash
# Get latest version
VERSION=$(curl -s https://api.github.com/repos/nektos/act/releases/latest | \
  grep '"tag_name":' | sed -E 's/.*"v([^"]+)".*/\1/')

# Download for your platform
# Linux x86_64:
curl -L -o act.tar.gz \
  "https://github.com/nektos/act/releases/download/v${VERSION}/act_Linux_x86_64.tar.gz"

# Linux ARM64:
curl -L -o act.tar.gz \
  "https://github.com/nektos/act/releases/download/v${VERSION}/act_Linux_arm64.tar.gz"

# macOS Intel:
curl -L -o act.tar.gz \
  "https://github.com/nektos/act/releases/download/v${VERSION}/act_Darwin_x86_64.tar.gz"

# macOS Apple Silicon:
curl -L -o act.tar.gz \
  "https://github.com/nektos/act/releases/download/v${VERSION}/act_Darwin_arm64.tar.gz"
```

#### Step 2: Extract and Verify

```bash
tar xzf act.tar.gz
./act --version
```

#### Step 3: Install

**Option A: System-wide (requires sudo)**
```bash
sudo install -m 755 act /usr/local/bin/act
```

**Option B: User-local (no sudo required)**
```bash
mkdir -p ~/.local/bin
install -m 755 act ~/.local/bin/act

# Add to PATH (add this to ~/.bashrc or ~/.zshrc)
export PATH="$HOME/.local/bin:$PATH"
```

#### Step 4: Verify

```bash
act --version
which act
```

#### Step 5: Clean up

```bash
rm act.tar.gz LICENSE README.md
```

### Method 3: Homebrew (If Available)

If you already have Homebrew/Linuxbrew:

```bash
# macOS
brew install act

# Linux with Linuxbrew
brew install act
```

To install Homebrew on Linux:
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Method 4: Build from Source (For Paranoid)

If you want absolute certainty:

```bash
# Requires Go 1.21+
git clone https://github.com/nektos/act.git
cd act
make build
sudo cp dist/local/act /usr/local/bin/
```

## Verification After Installation

### Check Version

```bash
act --version
# Should show: act version 0.2.x
```

### Check Binary Location

```bash
which act
# Should show: /usr/local/bin/act or ~/.local/bin/act
```

### Test with Dry Run

```bash
cd /path/to/libfyaml-ci
act -l
```

## Keeping act Updated

### With Homebrew

```bash
brew upgrade act
```

### Manual Installation

Repeat the installation steps with the new version.

### Check for Updates

```bash
# Current version
act --version

# Latest version
curl -s https://api.github.com/repos/nektos/act/releases/latest | \
  grep '"tag_name":' | sed -E 's/.*"tag_name": "v([^"]+)".*/\1/'
```

## Platform-Specific Notes

### Debian/Ubuntu

No official package - use manual installation or Homebrew.

```bash
# Quick user-local install (no sudo)
./install-act-safe.sh
```

### Arch Linux

Available in AUR:

```bash
yay -S act
# or
paru -S act
```

### Fedora/RHEL

No official package - use manual installation.

### macOS

Homebrew is recommended:

```bash
brew install act
```

### Windows

Not needed for libfyaml development (use WSL2 instead).

## Docker Alternative (No Installation)

If you don't want to install act at all:

```bash
# Run act in Docker
docker run -v /var/run/docker.sock:/var/run/docker.sock \
  -v $(pwd):/repo \
  -w /repo \
  nektos/act:latest -l
```

Create an alias:
```bash
echo 'alias act="docker run -v /var/run/docker.sock:/var/run/docker.sock -v \$(pwd):/repo -w /repo nektos/act:latest"' >> ~/.bashrc
```

## Troubleshooting

### act command not found

```bash
# If installed to ~/.local/bin, ensure it's in PATH
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Permission denied

```bash
# If downloaded binary isn't executable
chmod +x ~/.local/bin/act
```

### Wrong architecture

```bash
# Check your architecture
uname -m
# x86_64 → use x86_64 binary
# aarch64 → use arm64 binary
# armv7l → use armv7 binary
```

## Security Best Practices

1. **Always download from official sources**
   - GitHub releases: https://github.com/nektos/act/releases
   - Official repository only

2. **Verify checksums** (optional but recommended)
   ```bash
   # Download checksum file
   curl -L -o checksums.txt \
     "https://github.com/nektos/act/releases/download/v${VERSION}/checksums.txt"

   # Verify
   sha256sum -c checksums.txt 2>&1 | grep OK
   ```

3. **Review before executing**
   - Never pipe curl to bash with sudo
   - Always download, inspect, then run

4. **Use user-local installation when possible**
   - No sudo required
   - Limits potential damage
   - Easy to uninstall

## Comparison of Methods

| Method | Security | Ease | Updates |
|--------|----------|------|---------|
| **install-act-safe.sh** | High | Easy | Manual |
| **Manual installation** | Highest | Medium | Manual |
| **Homebrew** | High | Easiest | Automatic |
| **Build from source** | Highest | Hard | Manual |
| **curl &#124; sudo bash** | ⚠️ Low | Easiest | Manual |

## Uninstalling act

```bash
# If installed system-wide
sudo rm /usr/local/bin/act

# If installed user-local
rm ~/.local/bin/act

# If installed via Homebrew
brew uninstall act
```

## Quick Reference

```bash
# Safe installation (recommended)
./install-act-safe.sh

# Or manual (one command)
VERSION=$(curl -s https://api.github.com/repos/nektos/act/releases/latest | grep '"tag_name":' | sed -E 's/.*"v([^"]+)".*/\1/') && \
curl -L "https://github.com/nektos/act/releases/download/v${VERSION}/act_Linux_x86_64.tar.gz" | \
tar xz && \
mkdir -p ~/.local/bin && \
install -m 755 act ~/.local/bin/act && \
rm LICENSE README.md && \
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc

# Verify
~/.local/bin/act --version
```

## Getting Help

- **act documentation**: https://github.com/nektos/act
- **GitHub releases**: https://github.com/nektos/act/releases
- **Issues**: https://github.com/nektos/act/issues
