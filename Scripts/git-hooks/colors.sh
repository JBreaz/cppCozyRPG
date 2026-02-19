#!/bin/sh

# --- Colored output helpers (shared) ---
if [ -t 1 ]; then
  RED="$(printf '\033[31m')"
  GREEN="$(printf '\033[32m')"
  YELLOW="$(printf '\033[33m')"
  CYAN="$(printf '\033[36m')"
  RESET="$(printf '\033[0m')"
else
  RED=""; GREEN=""; YELLOW=""; CYAN=""; RESET=""
fi

info()  { printf "%s[INFO] %s%s\n"  "$CYAN"   "$*" "$RESET"; }
warn()  { printf "%s[WARN] %s%s\n"  "$YELLOW" "$*" "$RESET"; }
error() { printf "%s[ERROR] %s%s\n" "$RED"    "$*" "$RESET"; }
ok()    { printf "%s[OK] %s%s\n"    "$GREEN"  "$*" "$RESET"; }
