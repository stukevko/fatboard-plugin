#!/bin/sh
# ---------------------------------------------------------------------------
#  Fatboard – Mac installer
#
#  Two ways to run it:
#
#   A) from a downloaded build (works today, no website needed)
#        unzip Fatboard-macOS-x.y.z.zip, open Terminal, type "sh " (with the
#        space), drag install.sh from that folder into the window, press Enter.
#
#   B) straight from the web (once fatboard.com is live)
#        curl -fsSL https://fatboard.com/mac | sh
#
#  Installs Scrub, Scrub Pro, Harun, Spit, Smash, Korrent and Thump (VST3 + AU) into your user
#  Library. No admin password, no Apple developer account needed: the script
#  removes the download quarantine flag and signs the plugins locally (ad hoc),
#  so macOS and your DAW load them like your own files.
#
#  Options (env vars):
#    FATBOARD_ONLY=scrub        web mode: one plugin (scrub | scrub-pro | harun | spit | smash | korrent | thump)
#    FATBOARD_BASE=https://…    web mode: different download host
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
if [ "${FATBOARD_SYSTEM:-0}" = "1" ]; then
  VST3_DIR="/Library/Audio/Plug-Ins/VST3"; AU_DIR="/Library/Audio/Plug-Ins/Components"; SUDO="sudo"
else
  VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"; SUDO=""
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Local mode: the script sits next to the plugins (unzipped build).
SRC=""
case "$0" in
  *install.sh)
    HERE="$(cd "$(dirname "$0")" && pwd)"
    if ls "$HERE"/*.vst3 >/dev/null 2>&1; then SRC="$HERE"; fi ;;
esac

if [ -n "$SRC" ]; then
  say "${G}→ installing from $SRC${N}"
else
  command -v curl >/dev/null || die "curl not found"
  command -v unzip >/dev/null || die "unzip not found"
  say "${G}→ downloading ${ONLY} (mac)…${N}"
  curl -fSL --progress-bar "$BASE/dl/$ONLY/mac" -o "$TMP/fatboard.zip" || die "download failed – is $BASE reachable?"
  unzip -q "$TMP/fatboard.zip" -d "$TMP/x" || die "could not unzip"
  SRC="$TMP/x"
fi

$SUDO mkdir -p "$VST3_DIR" "$AU_DIR"

# A browser download marks files as "quarantined" and macOS then refuses unsigned
# plugins. Remove the mark and sign locally (ad hoc) – that is all a DAW needs.
fix() {
  $SUDO xattr -dr com.apple.quarantine "$1" 2>/dev/null || true
  if command -v codesign >/dev/null; then $SUDO codesign --force --deep --sign - "$1" >/dev/null 2>&1 || true; fi
}

n=0
find "$SRC" -name "*.vst3" -maxdepth 3 -type d | while read -r p; do
  name="$(basename "$p")"
  $SUDO rm -rf "$VST3_DIR/$name"
  $SUDO cp -R "$p" "$VST3_DIR/"
  fix "$VST3_DIR/$name"
  say "  ${Y}✓${N} VST3  $name"
done
find "$SRC" -name "*.component" -maxdepth 3 -type d | while read -r p; do
  name="$(basename "$p")"
  $SUDO rm -rf "$AU_DIR/$name"
  $SUDO cp -R "$p" "$AU_DIR/"
  fix "$AU_DIR/$name"
  say "  ${Y}✓${N} AU    $name"
done

# standalone apps (optional, handy for trying things without a DAW)
if ls "$SRC"/*.app >/dev/null 2>&1; then
  APP_DIR="$HOME/Applications/Fatboard"
  mkdir -p "$APP_DIR"
  for p in "$SRC"/*.app; do
    name="$(basename "$p")"
    if [ -n "$name" ] && [ -d "$APP_DIR/$name" ]; then rm -rf "$APP_DIR/$name"; fi
    cp -R "$p" "$APP_DIR/"
    xattr -dr com.apple.quarantine "$APP_DIR/$name" 2>/dev/null || true
    codesign --force --deep --sign - "$APP_DIR/$name" >/dev/null 2>&1 || true
  done
  say "  ${Y}✓${N} Apps  $APP_DIR"
fi
# make Logic/GarageBand re-scan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

say ""
say "${W}Done.${N} Restart your DAW and rescan plugins (FL Studio: Options > Manage plugins > Find installed plugins)."
say "${G}VST3 → $VST3_DIR${N}"
say "${G}AU   → $AU_DIR${N}"
say ""
say "${Y}Fett. Korrent.${N}"
say ""
