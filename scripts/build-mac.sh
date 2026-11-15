#!/bin/sh
# Fatboard – build everything on your own Mac and install it (VST3 + AU, into your user Library).
# Needs: Apple's command line tools (xcode-select --install) and CMake (brew install cmake).
# Run from anywhere:   sh ~/fatboard-plugin/scripts/build-mac.sh
# First run takes 10-25 minutes (it downloads and compiles JUCE); later runs only rebuild what changed.
set -e
cd "$(dirname "$0")/.."
xcode-select -p >/dev/null 2>&1 || { echo "Apple's command line tools are missing. Run:  xcode-select --install   then start this again."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "CMake is missing. Run:  brew install cmake   (Homebrew: https://brew.sh) then start this again."; exit 1; }

git pull --ff-only || echo "(could not pull, building what is here)"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Smash_All Sog_All Dig_All --parallel "$(sysctl -n hw.ncpu)"

VST="$HOME/Library/Audio/Plug-Ins/VST3"; AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST" "$AU"
for t in Scrub ScrubPro Harun Smash Sog Dig; do
  for kind in VST3 AU; do
    dir="build/${t}_artefacts/Release/$kind"; [ -d "$dir" ] || continue
    dest="$VST"; [ "$kind" = AU ] && dest="$AU"
    for b in "$dir"/*.vst3 "$dir"/*.component; do
      [ -e "$b" ] || continue
      name="$(basename "$b")"
      rm -rf "$dest/$name"; cp -R "$b" "$dest/"
      xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
      codesign --force --deep --sign - "$dest/$name" 2>/dev/null || true
      echo "installed $name"
    done
  done
done
killall -9 AudioComponentRegistrar 2>/dev/null || true      # so Logic / GarageBand see the new AUs
echo
echo "Done. Close FL Studio, open it again and rescan plugins (Dig is an instrument: Channel Rack)."
