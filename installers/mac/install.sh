#!/bin/sh
# ---------------------------------------------------------------------------
#  Fatboard – Mac installer
#
#    curl -fsSL https://fatboard.com/mac | sh
#
#  Installs Scrub, Scrub Pro and Harun (VST3 + AU) into your user Library.
#  No admin password, no Gatekeeper dialogs: files fetched by curl don't carry
#  the quarantine flag, so macOS treats them like your own files.
#
#  Options (env vars):
#    FATBOARD_ONLY=scrub        install one plugin (scrub | scrub-pro | harun)
#    FATBOARD_BASE=https://…    different download host (dev/testing)
#    FATBOARD_SYSTEM=1          install for all users (/Library, needs sudo)
#
#  Source: https://github.com/stukevko/fatboard-plugin/blob/main/installers/mac/install.sh
# ---------------------------------------------------------------------------
set -e

BASE="${FATBOARD_BASE:-https://fatboard.com}"
ONLY="${FATBOARD_ONLY:-all}"

Y='\033[1;33m'; W='\033[1;37m'; G='\033[0;90m'; R='\033[0;31m'; N='\033[0m'
say()  { printf "%b\n" "$1"; }
die()  { say "${R}✗ $1${N}"; exit 1; }

say ""
say "${Y}  ▬▬▬▬${N}"
say "${Y}  ▬▬▬▬▬${N}   ${W}FATBOARD${N}"
say "${Y}  ▬▬▬▬▬▬${N}  ${G}Thin in. Fat out.${N}"
say ""

[ "$(uname)" = "Darwin" ] || die "This installer is for macOS. Windows: download the setup .exe from fatboard.com"
command -v curl >/dev/null || die "curl not found"
command -v unzip >/dev/null || die "unzip not found"

if [ "${FATBOARD_SYSTEM:-0}" = "1" ]; then
  VST3_DIR="/Library/Audio/Plug-Ins/VST3"; AU_DIR="/Library/Audio/Plug-Ins/Components"; SUDO="sudo"
else
  VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"; SUDO=""
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say "${G}→ downloading ${ONLY} (mac)…${N}"
curl -fSL --progress-bar "$BASE/dl/$ONLY/mac" -o "$TMP/fatboard.zip" || die "download failed – is $BASE reachable?"
unzip -q "$TMP/fatboard.zip" -d "$TMP/x" || die "could not unzip"

$SUDO mkdir -p "$VST3_DIR" "$AU_DIR"

n=0
find "$TMP/x" -name "*.vst3" -maxdepth 3 -type d | while read -r p; do
  name="$(basename "$p")"
  $SUDO rm -rf "$VST3_DIR/$name"
  $SUDO cp -R "$p" "$VST3_DIR/"
  say "  ${Y}✓${N} VST3  $name"
done
find "$TMP/x" -name "*.component" -maxdepth 3 -type d | while read -r p; do
  name="$(basename "$p")"
  $SUDO rm -rf "$AU_DIR/$name"
  $SUDO cp -R "$p" "$AU_DIR/"
  say "  ${Y}✓${N} AU    $name"
done

# belt and braces: strip quarantine if a browser ever touched these
$SUDO xattr -dr com.apple.quarantine "$VST3_DIR" "$AU_DIR" 2>/dev/null || true
# make Logic/GarageBand re-scan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

say ""
say "${W}Done.${N} Rescan plugins in your DAW (or just restart it)."
say "${G}VST3 → $VST3_DIR${N}"
say "${G}AU   → $AU_DIR${N}"
say ""
say "${Y}Fett. Brett.${N}"
say ""
