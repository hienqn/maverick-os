#!/bin/bash
# MaverickOS/PintOS Setup Script
# This script installs all required dependencies for building and testing.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command() {
    if command -v "$1" &> /dev/null; then
        echo -e "${GREEN}[OK]${NC} $1 is installed"
        return 0
    else
        echo -e "${RED}[MISSING]${NC} $1 is not installed"
        return 1
    fi
}

# Detect OS
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if command -v apt-get &> /dev/null; then
            echo "debian"
        elif command -v dnf &> /dev/null; then
            echo "fedora"
        elif command -v pacman &> /dev/null; then
            echo "arch"
        else
            echo "linux-unknown"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    else
        echo "unknown"
    fi
}

install_debian() {
    log_info "Installing dependencies for Debian/Ubuntu..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        gcc-multilib \
        g++-multilib \
        gcc-i686-linux-gnu \
        g++-i686-linux-gnu \
        binutils-i686-linux-gnu \
        qemu-system-x86 \
        qemu-utils \
        make \
        git \
        curl \
        unzip
}

install_fedora() {
    log_info "Installing dependencies for Fedora..."
    sudo dnf install -y \
        @development-tools \
        glibc-devel.i686 \
        gcc-c++ \
        qemu-system-x86 \
        qemu-img \
        make \
        git \
        curl \
        unzip
    log_warn "Cross-compiler for i686 may need manual installation on Fedora"
}

install_arch() {
    log_info "Installing dependencies for Arch Linux..."
    sudo pacman -S --needed \
        base-devel \
        lib32-glibc \
        qemu-system-x86 \
        qemu-img \
        make \
        git \
        curl \
        unzip
    log_warn "Cross-compiler for i686 may need AUR package"
}

install_macos() {
    log_info "Installing dependencies for macOS..."
    if ! command -v brew &> /dev/null; then
        log_error "Homebrew is required. Install from https://brew.sh"
        exit 1
    fi
    brew install qemu make git curl
    log_warn "Cross-compiler for i686-linux-gnu needs manual setup on macOS"
    log_warn "Consider using a Docker container or VM for building"
}

install_bun() {
    if command -v bun &> /dev/null; then
        log_info "bun is already installed: $(bun --version)"
        return 0
    fi

    if [ -f "$HOME/.bun/bin/bun" ]; then
        log_info "bun found at ~/.bun/bin/bun, adding to PATH..."
        export PATH="$HOME/.bun/bin:$PATH"
        return 0
    fi

    log_info "Installing bun..."
    curl -fsSL https://bun.sh/install | bash

    # Add to current session
    export PATH="$HOME/.bun/bin:$PATH"

    log_info "bun installed successfully"
}

setup_path() {
    log_info "Setting up PATH..."

    SHELL_RC=""
    if [ -n "$ZSH_VERSION" ] || [ -f "$HOME/.zshrc" ]; then
        SHELL_RC="$HOME/.zshrc"
    elif [ -n "$BASH_VERSION" ] || [ -f "$HOME/.bashrc" ]; then
        SHELL_RC="$HOME/.bashrc"
    fi

    if [ -n "$SHELL_RC" ]; then
        # Add bun to PATH if not already there
        if ! grep -q 'export BUN_INSTALL' "$SHELL_RC" 2>/dev/null; then
            echo '' >> "$SHELL_RC"
            echo '# bun' >> "$SHELL_RC"
            echo 'export BUN_INSTALL="$HOME/.bun"' >> "$SHELL_RC"
            echo 'export PATH="$BUN_INSTALL/bin:$PATH"' >> "$SHELL_RC"
            log_info "Added bun to $SHELL_RC"
        fi
    fi
}

verify_installation() {
    echo ""
    log_info "Verifying installation..."
    echo ""

    local all_ok=true

    check_command "make" || all_ok=false
    check_command "git" || all_ok=false
    check_command "qemu-system-i386" || all_ok=false

    # Check for cross-compiler
    if check_command "i686-linux-gnu-gcc"; then
        :
    elif check_command "i686-elf-gcc"; then
        log_warn "Using i686-elf-gcc instead of i686-linux-gnu-gcc"
    else
        all_ok=false
    fi

    # Check for bun
    if [ -f "$HOME/.bun/bin/bun" ]; then
        echo -e "${GREEN}[OK]${NC} bun is installed at ~/.bun/bin/bun"
    elif check_command "bun"; then
        :
    else
        all_ok=false
    fi

    echo ""
    if $all_ok; then
        log_info "All dependencies are installed!"
        echo ""
        log_info "To build and test:"
        echo "    cd src/threads && make check"
        echo "    cd src/userprog && make check"
        echo "    cd src/vm && make check"
        echo "    cd src/filesys && make check"
    else
        log_warn "Some dependencies are missing. Please install them manually."
    fi
}

main() {
    echo "========================================"
    echo "  MaverickOS/PintOS Setup Script"
    echo "========================================"
    echo ""

    OS=$(detect_os)
    log_info "Detected OS: $OS"
    echo ""

    case "$1" in
        --check)
            verify_installation
            exit 0
            ;;
        --bun-only)
            install_bun
            setup_path
            verify_installation
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTION]"
            echo ""
            echo "Options:"
            echo "  --check      Only verify installed dependencies"
            echo "  --bun-only   Only install bun runtime"
            echo "  --help       Show this help message"
            echo ""
            echo "Without options, installs all dependencies."
            exit 0
            ;;
    esac

    case "$OS" in
        debian)
            install_debian
            ;;
        fedora)
            install_fedora
            ;;
        arch)
            install_arch
            ;;
        macos)
            install_macos
            ;;
        *)
            log_warn "Unknown OS. Please install dependencies manually:"
            echo "  - GCC cross-compiler (i686-linux-gnu-gcc or i686-elf-gcc)"
            echo "  - QEMU (qemu-system-i386)"
            echo "  - bun runtime (https://bun.sh)"
            echo "  - make, git"
            ;;
    esac

    install_bun
    setup_path
    verify_installation
}

main "$@"
