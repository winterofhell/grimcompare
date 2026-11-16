#!/usr/bin/env bash
# GD Compare setup for Linux: run ./install.sh, or double-click it (it opens a terminal).
cd "$(dirname "$(readlink -f "$0")")" || exit 1
BIN=./bin/gdcompare-setup-linux
chmod +x "$BIN" 2>/dev/null  # some unzip tools drop the executable bit

if [[ ! -t 0 || ! -t 1 ]] && [[ -z "${GDC_IN_TERMINAL:-}" ]]; then
  export GDC_IN_TERMINAL=1
  self="$PWD/install.sh"
  if command -v x-terminal-emulator >/dev/null; then exec x-terminal-emulator -e "$self" "$@"
  elif command -v gnome-terminal >/dev/null; then exec gnome-terminal -- "$self" "$@"
  elif command -v konsole >/dev/null; then exec konsole -e "$self" "$@"
  elif command -v xfce4-terminal >/dev/null; then exec xfce4-terminal -x "$self" "$@"
  elif command -v alacritty >/dev/null; then exec alacritty -e "$self" "$@"
  elif command -v kitty >/dev/null; then exec kitty "$self" "$@"
  elif command -v foot >/dev/null; then exec foot "$self" "$@"
  elif command -v xterm >/dev/null; then exec xterm -e "$self" "$@"
  fi
  echo "No terminal found. Open a terminal in this folder and run ./install.sh" >&2
  exit 1
fi

"$BIN" "$@"
status=$?
echo
read -rp "Press Enter to close. " _
exit $status
