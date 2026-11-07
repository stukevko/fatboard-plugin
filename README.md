# Fatboard – Cleanup & Pro (v0.1, Testbuild)

Zwei Plugins, ein Projekt, ein Look.

| Plugin | Was | Preis | Bedienung |
|---|---|---|---|
| **Fatboard Cleanup** | Nimmt Dröhnen/Resonanzen automatisch raus, gleicht dumpfe oder harsche Höhen aus, 22-Hz-Rumble-Filter | free | ein Knopf: **Amount** |
| **Fatboard Pro** | Cleanup + drei Vocal-Ketten **Crisp / Clear / Soft** (HPF, Tone-EQ, De-Esser, Sättigung) | 20 $ | zwei Knöpfe: **Cleanup**, **Mode** + drei Pills |

Formate: VST3 (Win/Mac), AU (Mac), Standalone zum schnellen Testen.
Kein Analyzer, keine Frequenzanzeige, kein Netz, kein Account – alles läuft lokal.

---

## Schnellster Weg zu Testbuilds: GitHub Actions

1. Neues (privates) GitHub-Repo anlegen, diesen Ordner pushen (Branch `main`).
2. Reiter **Actions** → Workflow „Build Fatboard plugins" läuft automatisch (ca. 10–15 min).
3. Unten unter **Artifacts** liegen `Fatboard-Windows.zip` und `Fatboard-macOS.zip` mit VST3, AU und Standalone.

Der Ordner `JUCE/` ist absichtlich nicht dabei (`.gitignore`) – CMake lädt JUCE 8.0.4 selbst nach.

## Lokal bauen

### Windows (FL Studio, Ableton, …)
Voraussetzungen: **Visual Studio 2022 Community** (Workload „Desktopentwicklung mit C++") und **CMake** (ist bei VS dabei).

```bat
cd fatboard-plugins
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target FatboardCleanup_All FatboardPro_All
```

Ergebnis:
- `build\FatboardCleanup_artefacts\Release\VST3\Fatboard Cleanup.vst3`
- `build\FatboardPro_artefacts\Release\VST3\Fatboard Pro.vst3`
- `...\Release\Standalone\Fatboard Cleanup.exe` / `Fatboard Pro.exe`

Die `.vst3`-Ordner nach `C:\Program Files\Common Files\VST3\` kopieren, DAW neu scannen.
FL Studio: Options → Manage Plugins → „Find plugins".

### macOS (Logic, Ableton, …)
Voraussetzungen: **Xcode** (App Store) + Command Line Tools, **CMake** (`brew install cmake`).

```sh
cd fatboard-plugins
cmake -B build -G Xcode
cmake --build build --config Release --target FatboardCleanup_All FatboardPro_All
```

Ergebnis in `build/*_artefacts/Release/{VST3,AU,Standalone}/`.
VST3 → `~/Library/Audio/Plug-Ins/VST3/`, AU → `~/Library/Audio/Plug-Ins/Components/`.
Unsignierte Builds: einmal `xattr -cr "~/Library/Audio/Plug-Ins/VST3/Fatboard Pro.vst3"` (und Cleanup), sonst blockt Gatekeeper. Für AU in Logic ggf. `killall -9 AudioComponentRegistrar` und Logic neu starten.

### Linux (nur zum Entwickeln)
```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FatboardCleanup_VST3 FatboardPro_VST3
```

---

## Wo du beim Testen drehst (alles nach Gehör)

Alle Klangentscheidungen sind als `// TUNE` markiert und liegen an zwei Stellen:

- **`Source/Common/CleanupEngine.h` → `struct Settings`**
  Schwellen (`thresholdDb`, `thresholdLowDb`), wie viel vom Überstand weg kommt (`ratio`), Deckel (`maxCutDb`), Breite der Cuts (`bandQ`), Attack/Release, Höhen-Balance (`shelfTargetDb`, `shelfMaxDb`, `shelfStrength`).
  Faustregel: *Klingt es dünn → `ratio` runter oder `thresholdDb` hoch. Dröhnt es noch → `thresholdLowDb` runter, `maxCutLowDb` hoch.*

- **`Source/Common/VocalModes.h` → `Crisp`, `Clear`, `Soft`**
  Pro Modus: HPF, Low-Shelf, Low-Mid-Cut, Box-Cut, Presence, High-Shelf, De-Esser (Frequenz, max. Cut, Schwelle), Sättigung (`drive`), Trim. Der Mode-Knopf skaliert alle dB-Werte und die Sättigung.

Ein Offline-Test ohne DAW: `cmake -B build -DFATBOARD_BUILD_TESTS=ON`, dann Target `FatboardEngineTest` bauen und starten. Er druckt pro Band, was das Plugin an einem synthetischen Signal (Resonanz bei 180 Hz, dumpfe Höhen) macht.

## Design

- Palette, Fonts, Knopf, Pills, Balken-Logo: `Source/Common/FatboardLookAndFeel.*`
- Header, großer Knopf, Aktivitäts-Logo, Footer: `Source/Common/FatboardWidgets.h`
- Fenster/Layout: `Source/Cleanup/CleanupEditor.cpp` (420×540), `Source/Pro/ProEditor.cpp` (640×540)
- Schriften (Unbounded Black/Bold, Inter Medium/SemiBold) sind eingebettet, Lizenz SIL OFL in `Assets/`.

Die drei Balken unter dem Knopf leuchten mit der Arbeit des Plugins auf: dünner Balken = wenig, fetter Balken = viel. Das ist die einzige „Anzeige" – absichtlich.

## Struktur
```
CMakeLists.txt            beide Targets + optionaler Test
Assets/                   Fonts (eingebettet)
Source/Common/            Engine, Vocal-Modi, Look & Feel, Widgets
Source/Cleanup/           Cleanup-Plugin
Source/Pro/               Pro-Plugin
Tests/EngineTest.cpp      Offline-Sanity-Test
.github/workflows/        Win/Mac-Builds auf GitHub
```

## Bekannte Grenzen v0.1
- Keine Lizenzprüfung in Pro (kommt mit dem Shop: Paddle/Lemon Squeezy-Key).
- Kein Oversampling bei der Sättigung (bei hohem Mode-Amount leichte Aliasing-Spitzen möglich).
- Kein Resizing des Fensters.
- Kein AAX.
