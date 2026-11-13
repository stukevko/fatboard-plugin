# Scrub & Scrub Pro by Fatboard (v0.1, Testbuild)

Zwei Plugins, ein Projekt, ein Look.

| Plugin | Was | Preis | Bedienung |
|---|---|---|---|
| **Scrub** | Nimmt Dröhnen/Resonanzen automatisch raus, gleicht dumpfe oder harsche Höhen aus, 22-Hz-Rumble-Filter | free | ein Knopf: **Amount** |
| **Harun** | Für die **aufgenommene** Double-/Harmony-Spur, ohne Sidechain: vier **Vorlagen** (Double, Wide, Harmony, Far). Jede macht aus dem Take den fertigen L/R-Layer (Haas-Versatz, Dekorrelation, mono-sicher), mit einem EQ aus der Scrub-Familie (gleiche Kurve, aber unten höher und oben tiefer beschnitten, breite Präsenz-Senke bei 3 kHz) und passendem **Hall/Echo** (gefiltert, de-esst, geduckt). | – | Vorlagen-Pills, **Width**, **Level**, **Space**, Space-Art Hall/Echo/Both, Kurvenanzeige |
| **Spit** | Beatbox → Drum-MIDI. Erkennt Onsets (Spectral Flux + Energiesprung) und klassifiziert Kick/Snare/Hat; pro Pad per „learn" (3× vormachen) auf deine Laute kalibrierbar. Live-MIDI-Out (C1/D1/F#1) und **Take**: Record → Beatboxen → MIDI-Chip in die DAW ziehen, quantisiert mit **Tight** aufs **Grid**. Knöpfe **Sens**, **Tight**. Latenz ~43 ms (PDC). | tbd | 3 Pads, 2 Knöpfe |
| **Smash** | Drum-Bus (ohne 808!): HPF 35 Hz → pegelbewusster Transient-Shaper → dynamischer Low-Mid-Cut (200–500) → Glue-Kompressor mit Auto-Schwelle → Hi-Tamer (6–9 k, dynamisch) → Tape-Sättigung → **Fit** (3-Band dyn. EQ, der die Drums dort zurücknimmt, wo das Sample auf dem Sidechain laut ist) → **Auto-Gain, pegelneutral**. Style Tight/Punch/Dirty, Knöpfe **Smash**, **Fit**. Null Latenz. | tbd | 1 Pill, 2 Knöpfe |
| **Korrent** | Master: macht den Song release-laut, 808 bleibt rund, Abhör-Check für Handy/Auto/Kopfhörer | offen | **Loud**-Knopf, Style Clean/Warm/Hard, Check Off/Phone/Car/Buds |
| **Scrub Pro** | Scrub + Kurvenanzeige + Feintuning der Stimmkurve (**Low Cut, Mud, Harsh, Top**) + **Air** (öffnet die Stimme, mit Zischlaut-Wächter) | 20 $ | großer **Scrub**-Knopf, **Air**, vier kleine Feintuning-Regler |

Formate: VST3 (Win/Mac), AU (Mac), Standalone zum schnellen Testen.
Farben: Scrub/Scrub Pro Gelb, Harun Orange ("Ember"), Spit Lime, Smash Magenta – eine Familie, auf einen Blick unterscheidbar.
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
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Spit_All Smash_All
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
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Spit_All Smash_All
```

Ergebnis in `build/*_artefacts/Release/{VST3,AU,Standalone}/`.
VST3 → `~/Library/Audio/Plug-Ins/VST3/`, AU → `~/Library/Audio/Plug-Ins/Components/`.
Unsignierte Builds: einmal `xattr -cr "~/Library/Audio/Plug-Ins/VST3/Scrub Pro.vst3"` (und Cleanup), sonst blockt Gatekeeper. Für AU in Logic ggf. `killall -9 AudioComponentRegistrar` und Logic neu starten.

### Linux (nur zum Entwickeln)
```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Scrub_VST3 ScrubPro_VST3 Harun_VST3 Spit_VST3 Smash_VST3
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


- **`Source/Spit/SpitEngine.h` → `Settings`, `resetPrototypes()`, `classify()`**
  Onset: Flux-Faktor und Energiesprung-Schwelle hängen an Sens; Totzeit 70 ms; Flux-only-Onsets < 150 ms nach einem Hit werden verworfen (Kick-Sweeps). Prototypen (low/mid/high-Anteil, Zentroid) sind Startwerte – „learn" überschreibt sie pro Nutzer und wird im Preset gespeichert. Offline-Test: `SpitTest` (synthetisches Beatbox-Pattern: 40/40 erkannt, 40/40 richtig).
  **In FL Studio:** Spit auf den Mikro-Mixer-Track; für Live-MIDI den Drum-Sampler auf Spits MIDI-Port hören lassen (Plugin-Wrapper → Output port setzen, Sampler → Input port gleich). Einfacher: Take aufnehmen und den Chip in den Piano-Roll ziehen.

- **`Source/Smash/SmashEngine.h` → `Settings`, `style()`**
  Pro Style: Transient-Betonung (dB), Ziel-GR, Attack/Release, Sättigung, Hi-Tamer- und Mud-Deckel. Fit-Bänder 250/1200/4000 Hz, max. 5 dB. Auto-Gain hält die Lautheit (±0,5 dB), damit nichts durch „lauter" gewinnt. Offline-Test: `SmashTest`.
  **In FL Studio:** Smash auf den Drum-Bus (Kick, Snare, Hats, Percs – **nicht** die 808). Sample-Track → Send auf den Drum-Bus mit „Sidechain to this track" → im Plugin „sample linked".

- **`Source/Common/VoiceCurve.h` → `FatboardCurve`** (Scrub + Scrub Pro: die feste Stimm-EQ-Kurve, Frequenzen/Gains/Q, `fullAt`)
- **`Source/Common/AirEngine.h` → `FatboardAir`** (Scrub Pro: Air-Shelves und Wächter)
  Bänder 300–6500 Hz mit Gewichtung (Sprache 1–4 kHz am stärksten), `maxDb` 4 pro Band (~5–6 dB im Sprachbereich bei 100 %), Attack 12 ms, Release 160 ms.
  **In FL Studio:** zweite Scrub-Pro-Instanz auf den Beat-/Sample-Bus, Modus Room; Vocal-Track → Send auf diesen Bus → „Sidechain to this track". Mode-Knopf = wie viel Platz.


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
Source/Spit/              Spit (Engine, UI)
Source/Smash/             Smash (Engine, UI)

Tests/EngineTest.cpp      Offline-Sanity-Test
.github/workflows/        Win/Mac-Builds auf GitHub
```

## Changelog
- **v0.8** – Neu: **Korrent**, der Master (`Source/Korrent/KorrentEngine.h`). Ein Knopf **Loud**: misst die K-gewichtete Lautheit, merkt sich die lauteste Stelle des Songs und setzt daraus EINE Verstärkung für den ganzen Song (Ziel −20 … −7 LUFS, Standard 70 % ≈ −11), Verse und Hook behalten ihren Abstand. Unter 110 Hz rundet ein Soft-Clipper die 808 ab, darüber Soft-Clip je Style (Clean/Warm/Hard) und ein Look-ahead-Limiter (3 ms Latenz, Ceiling −1 dB). **Check**: Abhör-Simulation Phone/Car/Buds, nur zum Hören: beim Rendern automatisch aus, nicht automatisierbar, wird nie als „an" gespeichert. Anzeige: Lautheits-Balken mit Zielzone. „Listen again" vergisst die gemerkte lauteste Stelle. Farbe Ice. Test: `KorrentTest`.
- **v0.7** – **Harun v2:** Sidechain und Auto-Level raus. Dafür vier Vorlagen (`HarunTemplates` in `Source/Harun/HarunEngine.h`): Double, Wide, Harmony, Far. Eine Vorlage legt den Klang fest (Scrub-Stimmkurve mit eigenem Low Cut/Top, Präsenz-Senke, Sättigung, Hall-Größe, Echo-Teilung) und schreibt passende Startwerte in Width, Level, Space und Space-Art; alles bleibt nachregelbar, Doppelklick = Wert der Vorlage. Der Raum ist die frühere Dafocus-Engine (`HarunSpace.h`: 8-Linien-FDN-Hall + Tempo-Echo, Send gefiltert und de-esst, vom Layer selbst geduckt). Kurvenanzeige wie in Scrub. **Scrub Pro:** Room und Sidechain-Eingang entfernt. Tests: `HarunTest` (alle Vorlagen + Stresstest), `CurveTest`. Startwerte: Scrub-Knopf 60 % (= exakt die EQ-Kurve), Air 0 % (aus, bis man es will). **Mac:** Der Build-Zip enthält jetzt `install.sh` (lokaler Modus: entpacken, im Terminal `sh install.sh`); das Skript installiert VST3 + AU ins Benutzer-Library, entfernt die Download-Quarantäne und signiert ad hoc. Die Bundles werden schon im CI ad hoc signiert. macOS-Build über „Run workflow" (Haken bei mac) oder Release-Tag.
- **v0.6** – Umbau nach Kevins Vorgabe. **Scrub** bekommt als feste Basis seine Standard-Vocal-EQ-Kurve (aus dem FL-Preset „Parametric EQ 2" ausgelesen): High Pass 131 Hz, −1,8 dB @ 192 Hz, −1,7 @ 313, −1,6 @ 634, −2,0 @ 1,35 kHz (schmal), −3,4 @ 3,13 kHz (sehr schmal), Low Pass 10 kHz. Der Knopf dreht Kurve und automatisches Cleanup zusammen rein, die volle Kurve steht ab 60 %, bei 0 % bit-identisch (`VoiceCurve.h`). **Scrub Pro** = dasselbe + **Air**-Regler (Mid-/High-Air-Shelf, ein Wächter nimmt die Anhebung bei S/T-Lauten für Millisekunden weg, `AirEngine.h`) + **Room** als Schalter (großer Knopf wird dann zur Room-Stärke, Stimmkurve/Cleanup/Air sind auf dem Beat aus). Raus: Modi Crisp/Clear/Soft, Auto Balance, Leveler. Scrub Pro komprimiert nicht mehr. Fenster 640×600, großer Scrub-Knopf. Tests: `CurveTest`, `RoomTest`. Nachtrag 12.11.: **Kurvenanzeige** in Scrub und Scrub Pro (`CurveView` in `FatboardWidgets.h`): eine Linie über 30 Hz–20 kHz zeigt live, was das Plugin gerade absenkt und anhebt (Stimmkurve, Automatik, Air, im Room-Modus die Room-Bänder). Kein Analyzer, sie zeigt das Plugin, nicht das Audio. Scrub Pro bekommt vier **Feintuning-Regler**, die die Stimmkurve aufteilen: Low Cut (20–250 Hz), Mud (0–200 % der Absenkungen bei 192/313/634 Hz), Harsh (0–200 % bei 1,35/3,13 kHz), Top (Low Pass 5–20 kHz). Standardwerte = das Preset, Doppelklick setzt zurück. Fenster Scrub 420×650, Scrub Pro 640×760. **Dafocus entfernt** (Kevins Entscheidung, 12.11.; der Code liegt in der Git-Historie bis Commit f53624f). **Room entfernt** (Scrub Pro hat keinen Sidechain-Eingang mehr; `RoomCarver.h` und `RoomTest` gelöscht, Code in der Historie bis c6d97b3).
- **v0.5** – (in v0.6 wieder entfernt) Scrub Pro: **Auto Balance + Leveler** in Crisp/Clear/Soft (`Source/Common/AutoBalance.h`). Auto Balance misst die Langzeit-Klangkurve der Stimme in vier breiten Bändern und zieht sie an die Zielkurve des Modus: zu viel 150–450 Hz runter, fehlende Präsenz und Air rauf, so viel wie die Aufnahme braucht (Grenzen: Low-Mid −6…+1,5 dB, Präsenz/Air −3…+6 dB). Der Leveler gleicht laute und leise Wörter an (Schwelle folgt dem eigenen Durchschnittspegel der Stimme, automatisches Makeup), dahinter ein Peak-Catcher. „Clean"-Regeln: nur Feed-Forward, nur breite Filter, Klang bewegt sich über Sekunden, Messung friert in Pausen ein, 1 dB Totzone, Amount 0 = bit-identisch. Der Mode-Knopf steuert alles mit, volle Korrektur ab ~60 %. Kein neuer Regler. Test: `BalanceTest`.
- **v0.4** – Scrub Pro: neuer Modus **Room** (Plugin auf den BEAT, Vocal auf den Sidechain: der Beat weicht in 6 Bändern nur dort und nur dann aus, wo die Stimme gerade ist; ohne Sidechain bit-transparent). Clear/Crisp heben Konsonanten dynamisch an (Verständlichkeit, `articDb`). Scrub-Knopf neu skaliert (`knobScale` 0,65): 100 % klingt jetzt wie früher ~65 %, „volle Pulle" bleibt natürlich. Test: `RoomTest`.
- **v0.3.2** – Neu: **Smash** (Drum-Bus mit Sample-Fit).
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
