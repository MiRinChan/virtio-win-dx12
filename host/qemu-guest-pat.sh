#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

qemu_binary='/usr/bin/qemu-system-x86_64'
if [[ "$qemu_binary" -ef "$0" ]]; then
  printf '%s\n' 'qemu_binary must point to the real QEMU executable.' >&2
  exit 64
fi

args=()
previous=''
for argument in "$@"; do
  if [[ "$previous" == '-accel' && ( "$argument" == 'kvm' || "$argument" == kvm,* ) ]]; then
    argument="$argument,honor-guest-pat=on"
  fi
  args+=("$argument")
  previous="$argument"
done
exec "$qemu_binary" "${args[@]}"
