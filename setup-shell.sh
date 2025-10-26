#!/bin/bash
# Quick setup script for shell environment
# Run this after container restarts: ./setup-shell.sh

echo "🔧 Setting up shell environment..."

# Install zsh, plugins, and clangd for code navigation
sudo apt-get update -qq
sudo apt-get install -y zsh zsh-syntax-highlighting zsh-autosuggestions clangd bear

# Set zsh as default shell
sudo chsh -s $(which zsh) $USER

# Fallback: Setup bashrc to auto-launch zsh (in case chsh doesn't persist)
if ! grep -q "exec zsh" ~/.bashrc; then
    echo 'if [ -t 1 ] && [ -x "$(command -v zsh)" ]; then exec zsh; fi' >> ~/.bashrc
fi

# Create symlink to .zshrc in project
ln -sf /home/workspace/group0/.zshrc ~/.zshrc

echo "✅ Done! Zsh is now your default shell. Restart your terminal."



