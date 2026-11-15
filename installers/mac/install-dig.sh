#!/bin/sh
# Dig (private tool) – install on a Mac. Unzip, then in Terminal:  sh install-dig.sh   (drag this file in after "sh ")
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
VST="$HOME/Library/Audio/Plug-Ins/VST3"; AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST" "$AU"
[ -d "$HERE/Dig.vst3" ] || { echo "Dig.vst3 is not next to this script."; exit 1; }
rm -rf "$VST/Dig.vst3" "$AU/Dig.component"
cp -R "$HERE/Dig.vst3" "$VST/"
[ -d "$HERE/Dig.component" ] && cp -R "$HERE/Dig.component" "$AU/"
for b in "$VST/Dig.vst3" "$AU/Dig.component"; do
  [ -e "$b" ] || continue
  xattr -dr com.apple.quarantine "$b" 2>/dev/null || true
  codesign --force --deep --sign - "$b" 2>/dev/null || true
done
echo "Dig is installed. Restart FL Studio and rescan plugins (it is an instrument: add it in the Channel Rack)."
