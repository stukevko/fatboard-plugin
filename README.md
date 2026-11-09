# Scrub & Scrub Pro by Fatboard (v0.1, Testbuild)

Zwei Plugins, ein Projekt, ein Look.

| Plugin | Was | Preis | Bedienung |
|---|---|---|---|
| **Scrub** | Nimmt Dröhnen/Resonanzen automatisch raus, gleicht dumpfe oder harsche Höhen aus, 22-Hz-Rumble-Filter | free | ein Knopf: **Amount** |
| **Harun** | Für die **aufgenommene** Double-/Harmony-Spur: macht aus einem Take den fertigen L/R-Layer – Haas-Versatz, Dekorrelation (mono-sicher), Tiefen raus, leicht gesättigt, **Auto-Level** unter dem Lead (Lead auf den Sidechain). Kein Pitching, keine Latenz. Mode Double/Harmony, Knöpfe **Amount**, **Width**, **Tuck**. | tbd | 1 Pill, 3 Knöpfe |
| **Dafocus** | Vocal-Raum: Hall (8-Linien-FDN) + Tempo-Delay auf einem gefilterten, de-essten Send, beides wird von der Stimme **geduckt** und blüht in Pausen auf. Space Tight/Room/Far, Echo Off/1/8/1/4/1/8-dot, Knöpfe **Amount**, **Duck**. BPM vom Host. | tbd | 2 Pills, 2 Knöpfe |
| **Spit** | Beatbox → Drum-MIDI. Erkennt Onsets (Spectral Flux + Energiesprung) und klassifiziert Kick/Snare/Hat; pro Pad per „learn" (3× vormachen) auf deine Laute kalibrierbar. Live-MIDI-Out (C1/D1/F#1) und **Take**: Record → Beatboxen → MIDI-Chip in die DAW ziehen, quantisiert mit **Tight** aufs **Grid**. Knöpfe **Sens**, **Tight**. Latenz ~43 ms (PDC). | tbd | 3 Pads, 2 Knöpfe |
| **Scrub Pro** | Cleanup + drei Vocal-Ketten **Crisp / Clear / Soft** (HPF, Tone-EQ, De-Esser, Sättigung) | 20 $ | zwei Knöpfe: **Cleanup**, **Mode** + drei Pills |

Formate: VST3 (Win/Mac), AU (Mac), Standalone zum schnellen Testen.
Farben: Scrub/Scrub Pro Gelb, Harun Orange ("Ember"), Dafocus Cyan ("Ice"), Spit Lime – eine Familie, auf einen Blick unterscheidbar.
Kein Analyzer, keine Frequenzanzeige, kein Netz, kein Account – alles läuft lokal.

---

## Testbuilds: GitHub Actions

- **Push auf `main`** → baut nur **Windows** (mit Cache, ab dem zweiten Lauf deutlich schneller). Artifact `Fatboard-Windows` mit Setup-.exe und Zip.
- **Mac** dazu: Actions → „Build Fatboard plugins" → **Run workflow** (Häkchen „Also build macOS" ist gesetzt). Der Mac-Build ist der langsame (~20 min, Universal-Binary).
- **Release**: Tag `v0.1.0` pushen → Windows + Mac + GitHub-Release.
- Reine Doku-Änderungen (`*.md`) lösen keinen Build aus; ein neuer Push bricht einen laufenden Build ab.

Der Ordner `JUCE/` ist absichtlich nicht dabei (`.gitignore`) – CMake lädt JUCE 8.0.4 selbst nach.

## Lokal bauen

### Windows (FL Studio, Ableton, …)
Voraussetzungen: **Visual Studio 2022 Community** (Workload „Desktopentwicklung mit C++") und **CMake** (ist bei VS dabei).

```bat
cd fatboard-plugins
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Dafocus_All Spit_All
```

Ergebnis:
- `build\Scrub_artefacts\Release\VST3\Scrub.vst3`
- `build\ScrubPro_artefacts\Release\VST3\Scrub Pro.vst3`
- `...\Release\Standalone\Scrub.exe` / `Scrub Pro.exe`

Die `.vst3`-Ordner nach `C:\Program Files\Common Files\VST3\` kopieren, DAW neu scannen.
FL Studio: Options → Manage Plugins → „Find plugins".

### macOS (Logic, Ableton, …)
Voraussetzungen: **Xcode** (App Store) + Command Line Tools, **CMake** (`brew install cmake`).

```sh
cd fatboard-plugins
cmake -B build -G Xcode
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Dafocus_All Spit_All
```

Ergebnis in `build/*_artefacts/Release/{VST3,AU,Standalone}/`.
VST3 → `~/Library/Audio/Plug-Ins/VST3/`, AU → `~/Library/Audio/Plug-Ins/Components/`.
Unsignierte Builds: einmal `xattr -cr "~/Library/Audio/Plug-Ins/VST3/Scrub Pro.vst3"` (und Cleanup), sonst blockt Gatekeeper. Für AU in Logic ggf. `killall -9 AudioComponentRegistrar` und Logic neu starten.

### Linux (nur zum Entwickeln)
```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Scrub_VST3 ScrubPro_VST3 Harun_VST3 Dafocus_VST3 Spit_VST3
```

---

## Wo du beim Testen drehst (alles nach Gehör)

Alle Klangentscheidungen sind als `// TUNE` markiert und liegen an zwei Stellen:

- **`Source/Common/CleanupEngine.h` → `struct Settings`**
  Schwellen (`thresholdDb`, `thresholdLowDb`), wie viel vom Überstand weg kommt (`ratio`), Deckel (`maxCutDb`), Breite der Cuts (`bandQ`), Attack/Release, Höhen-Balance (`shelfTargetDb`, `shelfMaxDb`, `shelfStrength`).
  Faustregel: *Klingt es dünn → `ratio` runter oder `thresholdDb` hoch. Dröhnt es noch → `thresholdLowDb` runter, `maxCutLowDb` hoch.*

- **`Source/Harun/HarunEngine.h` → `struct Settings`**
  Haas-Zeiten (8,5 / 13 ms), Tone des Layers (HPF, Low-Shelf, Presence, Air), All-Pass-Frequenzen für die Dekorrelation, Sättigung, Ziel-Pegel unter dem Lead (Double -6 dB, Harmony -8 dB), Auto-Level-Zeiten. Offline-Test: Target `HarunTest`.
  **In der DAW:** Harun auf die Double-Spur, Lead-Spur als Sidechain-Eingang routen (FL: Mixer-Send mit Sidechain, Ableton: Sidechain-Sektion im Plugin). Ohne Sidechain pegelt Harun gegen die eigene Lautheit.

- **`Source/Dafocus/DafocusEngine.h` → `Settings` + `preset()`**
  Send-Filter (HPF/LPF), De-Esser, Delay-Feedback und -Filter, Hall-Presets (RT60, Größe, Pre-Delay, Dämpfung, Breite), Ducking (Range, Attack, Release, Schwelle relativ zum Peak). Offline-Test: Target `DafocusTest`, optional mit WAV-Pfad.

- **`Source/Spit/SpitEngine.h` → `Settings`, `resetPrototypes()`, `classify()`**
  Onset: Flux-Faktor und Energiesprung-Schwelle hängen an Sens; Totzeit 70 ms; Flux-only-Onsets < 150 ms nach einem Hit werden verworfen (Kick-Sweeps). Prototypen (low/mid/high-Anteil, Zentroid) sind Startwerte – „learn" überschreibt sie pro Nutzer und wird im Preset gespeichert. Offline-Test: `SpitTest` (synthetisches Beatbox-Pattern: 40/40 erkannt, 40/40 richtig).
  **In FL Studio:** Spit auf den Mikro-Mixer-Track; für Live-MIDI den Drum-Sampler auf Spits MIDI-Port hören lassen (Plugin-Wrapper → Output port setzen, Sampler → Input port gleich). Einfacher: Take aufnehmen und den Chip in den Piano-Roll ziehen.

- **`Source/Common/VocalModes.h` → `Crisp`, `Clear`, `Soft`**
  Pro Modus: HPF, Low-Shelf, Low-Mid-Cut, Box-Cut, Presence, High-Shelf, De-Esser (Frequenz, max. Cut, Schwelle), Sättigung (`drive`), Trim. Der Mode-Knopf skaliert alle dB-Werte und die Sättigung.

Ein Offline-Test ohne DAW: `cmake -B build -DFATBOARD_BUILD_TESTS=ON`, dann Target `FatboardEngineTest` bauen und starten. Er druckt pro Band, was das Plugin an einem synthetischen Signal (Resonanz bei 180 Hz, dumpfe Höhen) macht.

## Design

- Palette, Fonts, Knopf, Pills, Balken-Logo: `Source/Common/FatboardLookAndFeel.*`
- Header, großer Knopf, Aktivitäts-Logo, Footer: `Source/Common/FatboardWidgets.h`
- Fenster/Layout: `Source/Scrub/CleanupEditor.cpp` (420×540), `Source/ScrubPro/ProEditor.cpp` (640×540)
- Schriften (Unbounded Black/Bold, Inter Medium/SemiBold) sind eingebettet, Lizenz SIL OFL in `Assets/`.

Die drei Balken unter dem Knopf leuchten mit der Arbeit des Plugins auf: dünner Balken = wenig, fetter Balken = viel. Das ist die einzige „Anzeige" – absichtlich.

## Struktur
```
CMakeLists.txt            beide Targets + optionaler Test
Assets/                   Fonts (eingebettet)
Source/Common/            Engine, Vocal-Modi, Look & Feel, Widgets
Source/Scrub/             Scrub (free)
Source/ScrubPro/          Scrub Pro
Source/Harun/             Harun (Engine, UI)
Source/Dafocus/           Dafocus (Engine, UI)
Source/Spit/              Spit (Engine, UI)

Tests/EngineTest.cpp      Offline-Sanity-Test
.github/workflows/        Win/Mac-Builds auf GitHub
```

## Changelog
- **v0.3.1** – Neu: **Spit** (Beatbox → Drum-MIDI).
- **v0.3** – Scrub/Scrub Pro: Tiefen deutlich sanfter (Schwelle 2,5→3,5 dB, Deckel 14→9 dB, 90–180 Hz extra geschützt), Release 90→220 ms, Pro-Ketten weniger Low-Cut: das Lebendige der Stimme bleibt. Harun komplett neu als L/R-Layer für aufgenommene Doubles (kein Pitching). Dafocus: Hall-Netzwerk neu, mathematisch stabil, Limiter + NaN-Schutz, Stresstest (`DafocusStress`). Pro-Plugin-Farben.

## Bekannte Grenzen v0.1
- Keine Lizenzprüfung in Pro (kommt mit dem Shop: Paddle/Lemon Squeezy-Key).
- Kein Oversampling bei der Sättigung (bei hohem Mode-Amount leichte Aliasing-Spitzen möglich).
- Kein Resizing des Fensters.
- Kein AAX.

## Installer

**Windows** – `Fatboard-Setup-<version>.exe` (Inno Setup, wird in CI gebaut aus `installers/windows/fatboard.iss`). Installiert VST3 nach `Common Files\VST3`, optional Standalone-Apps. Unsigniert → SmartScreen einmal "Weitere Informationen → Trotzdem ausführen".

**macOS** – kein `.dmg`, sondern eine Zeile im Terminal (kein Apple-Developer-Konto nötig, keine Gatekeeper-Dialoge):

```sh
curl -fsSL https://fatboard.com/mac | sh
```

Skript: `installers/mac/install.sh`. Es lädt `fatboard.com/dl/all/mac` (Weiterleitung auf das aktuelle Release-Zip), kopiert VST3 + AU in `~/Library/Audio/Plug-Ins/`, entfernt vorsorglich das Quarantäne-Flag und stößt den AU-Scan an. `FATBOARD_ONLY=scrub` installiert nur ein Plugin, `FATBOARD_SYSTEM=1` für alle Nutzer.

**Release**: Tag pushen (`git tag v0.1.0 && git push --tags`) → Actions baut alles und legt ein GitHub-Release mit `.exe` und den Mac-Zips an. Die Download-URLs dann im Web-Admin (Overview → Downloads) eintragen; `/dl/<produkt>/<plattform>` zeigt darauf.
