#!/bin/bash
#
# Safe installation of act from GitHub releases
# Alternative to: curl | sudo bash
#

set -e

echo "=========================================="
echo "Safe act Installation from GitHub Releases"
echo "=========================================="
echo ""

# Get latest version
echo "Fetching latest version..."
VERSION=$(curl -s https://api.github.com/repos/nektos/act/releases/latest | grep '"tag_name":' | sed -E 's/.*"v([^"]+)".*/\1/')
echo "Latest act version: v${VERSION}"
echo ""

# Determine architecture
ARCH=$(uname -m)
case $ARCH in
    x86_64)
        ARCH="x86_64"
        ;;
    aarch64|arm64)
        ARCH="arm64"
        ;;
    armv7l)
        ARCH="armv7"
        ;;
    *)
        echo "Error: Unsupported architecture: $ARCH"
        exit 1
        ;;
esac
echo "Architecture: $ARCH"
echo ""

# Determine OS
OS=$(uname -s)
case $OS in
    Linux)
        OS="Linux"
        ;;
    Darwin)
        OS="Darwin"
        ;;
    *)
        echo "Error: Unsupported OS: $OS"
        exit 1
        ;;
esac
echo "Operating System: $OS"
echo ""

# Download URL
URL="https://github.com/nektos/act/releases/download/v${VERSION}/act_${OS}_${ARCH}.tar.gz"
echo "Download URL: ${URL}"
echo ""

# Download to temp directory
TMPDIR=$(mktemp -d)
echo "Downloading to: $TMPDIR"
cd "$TMPDIR"
curl -L -o act.tar.gz "$URL"

# Extract
echo "Extracting..."
tar xzf act.tar.gz

# Verify binary exists
if [ ! -f "act" ]; then
    echo "Error: act binary not found in archive"
    exit 1
fi

# Show binary info
echo ""
echo "=========================================="
echo "Binary Downloaded Successfully"
echo "=========================================="
ls -lh act
./act --version
echo ""

# Installation options
echo "=========================================="
echo "Installation Options"
echo "=========================================="
echo ""
echo "Option 1: System-wide installation (requires sudo)"
echo "  sudo install -m 755 $TMPDIR/act /usr/local/bin/act"
echo ""
echo "Option 2: User installation (no sudo required)"
echo "  mkdir -p ~/.local/bin"
echo "  install -m 755 $TMPDIR/act ~/.local/bin/act"
echo "  echo 'export PATH=\$HOME/.local/bin:\$PATH' >> ~/.bashrc"
echo "  source ~/.bashrc"
echo ""
echo "Option 3: Keep in temp directory (for testing)"
echo "  $TMPDIR/act --version"
echo ""

# Ask user what to do
read -p "Install system-wide? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo install -m 755 "$TMPDIR/act" /usr/local/bin/act
    echo "✓ Installed to /usr/local/bin/act"
    echo ""
    act --version
    rm -rf "$TMPDIR"
    echo "✓ Cleaned up temp directory"
else
    echo ""
    echo "Binary left in: $TMPDIR"
    echo "To install later, run one of the commands above"
    echo "To clean up: rm -rf $TMPDIR"
fi

echo ""
echo "=========================================="
echo "Installation Complete"
echo "=========================================="
