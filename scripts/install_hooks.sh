#!/usr/bin/env bash
# Install git hooks into .git/hooks

set -e

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOKS_DIR="$REPO_ROOT/.git/hooks"

mkdir -p "$HOOKS_DIR"
cp "$REPO_ROOT/scripts/pre-push" "$HOOKS_DIR/pre-push"
chmod +x "$HOOKS_DIR/pre-push"
chmod +x "$REPO_ROOT/scripts/pre-push"
chmod +x "$REPO_ROOT/scripts/bump_version.py"

echo "Git pre-push hook installed successfully in $HOOKS_DIR/pre-push"
