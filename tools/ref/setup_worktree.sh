#!/bin/sh
# Link the private data of the main checkout into another worktree's .local.
# Usage (from the worktree root): tools/ref/setup_worktree.sh /path/to/main/checkout
set -e
main=${1:-/home/claude/work/inthehunt}
mkdir -p .local/ref .local/bin
for d in roms world; do [ -e ".local/$d" ] || ln -s "$main/.local/$d" ".local/$d"; done
for d in "$main"/.local/ref/scout-*; do [ -e ".local/ref/$(basename "$d")" ] || ln -s "$d" ".local/ref/$(basename "$d")"; done
echo "linked roms, world and scout recordings from $main"
