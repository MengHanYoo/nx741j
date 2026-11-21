#!/bin/bash
# Resolve the local Linux build inputs without baking one user's home directory
# into every build entry point. Callers may override each value explicitly.

resolve_unixnu_tree() {
  local requested=${1:-${UNIXNU:-}}
  local candidate
  if [ -n "$requested" ]; then
    if [ -x "$requested/unixnu" ]; then
      printf '%s\n' "$requested"
      return 0
    fi
    return 1
  fi
  for candidate in \
    "/opt/work/nx741j-build/UniXNU" \
    "/home/loongko/nx741j-build/UniXNU" \
    "${HOME:-}/nx741j-build/UniXNU" \
    "/mnt/e/unixnu-build/UniXNU"; do
    if [ -n "$candidate" ] && [ -x "$candidate/unixnu" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

resolve_uefi_tree() {
  local requested=${1:-${UNIXNU_UEFI:-}}
  local candidate
  if [ -n "$requested" ]; then
    if [ -f "$requested/Platforms/ZTE/nx741jPkg/nx741j.dsc" ]; then
      printf '%s\n' "$requested"
      return 0
    fi
    return 1
  fi
  for candidate in \
    "/opt/work/uefi-sheng-main" \
    "/home/loongko/uefi-sheng-main" \
    "/mnt/e/default-workspace/uefi-sheng-main"; do
    if [ -n "$candidate" ] && [ -f "$candidate/Platforms/ZTE/nx741jPkg/nx741j.dsc" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  for candidate in /opt/work/* /home/loongko/*; do
    if [ -f "$candidate/Platforms/ZTE/nx741jPkg/nx741j.dsc" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

resolve_artifact_dir() {
  local requested=${1:-}
  local candidate
  for candidate in \
    "$requested" \
    "${UNIXNU_ARTIFACTS:-}" \
    "/mnt/e/default-workspace/z80u-collect/_tmp"; do
    if [ -n "$candidate" ]; then
      mkdir -p "$candidate"
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}
