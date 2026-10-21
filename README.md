# Goat GiG Player

A VST3 instrument that plays **.gig** sample files (GigaStudio / Gigasampler format),
for example the Vienna orchestral instruments. Made for Windows and Reason 14.

It reads the files with [libgig](https://www.linuxsampler.org/libgig/) and uses
[JUCE](https://juce.com) for the plugin.

## Get the plugin

1. Open the **Releases** page of this repository.
2. Download `GigGoatPlayer-Windows.zip` from the release called **Latest Windows build**.
   (If there is no release yet, open the **Actions** tab and wait for the green tick.
   The zip is also saved on that run's page.)
3. Unzip it.

The zip has:

- `Goat GiG Player.vst3` (the plugin)
- `Goat GiG Player.exe` (a stand-alone version, handy for a quick test with a MIDI keyboard)
- `Orchestra of Goats GiG Player.vst3` and `.exe` (the multi-part version, see below)

## Install in Reason 14

1. Copy the whole **Goat GiG Player.vst3** folder into
   `C:\Program Files\Common Files\VST3`
2. Start Reason. If the plugin does not show up, open Reason's Preferences,
   find the Plugins page, and scan for plugins again.
3. Add **Goat GiG Player** as an instrument.

## Use it

1. Click **Open .gig file...** and pick a file.
2. Choose an instrument from the list (the arrows step through them).
3. Play MIDI notes.

The plugin remembers the file and instrument when you save your Reason song.

Big libraries come in several parts (`.gig`, `.gx01`, `.gx02` ...). Keep all the parts
in the same folder and open the `.gig` file.

### Controls

| Control | What it does |
|---|---|
| Volume | Overall level (-48 to +12 dB) |
| Tune | Fine tuning (+/- 100 cents) |
| Extra attack | Adds time to the start of every note (slower fade-in for strings) |
| Release time | Makes the note end faster or slower than the file says (1.0 = as in the file) |
| RAM per sample | How much of each sample is kept in memory. Bigger is safer on slow disks, smaller saves RAM |

The bottom line shows how many voices are playing, how much RAM the instrument uses,
and **disk underruns**. If underruns keep going up, pick a larger RAM setting or
use a faster drive.

## What it plays

- 16 bit and 24 bit samples, mono and stereo, normal and compressed (.gig compression)
- Velocity layers
- Loops (forward loops)
- Release samples (the sound that plays when you let go of a key)
- Round robin and random sample choice
- Layers, with crossfades that follow velocity, a MIDI controller or channel pressure
- Key switches
- Dimensions that follow MIDI controllers (mod wheel, breath, foot and so on)
- Left and right sample pairs
- Pan, sample volume, pitch, fine tune, sample start offset
- The volume envelope from the file: attack, hold, decay, sustain, release
- Sustain pedal (CC 64), pitch bend, CC 7 volume, all notes off
- Disk streaming: only the first part of each sample sits in RAM, the rest is read
  from disk while you play

## What it does NOT do (yet)

- Filters and LFOs from the file are ignored
- Velocity or controller influence on envelope times is ignored
- Ping-pong and backward loops play as normal forward loops
- GigaStudio instrument scripts and the legato / alternator rules are ignored
- The release-sample volume change with note length is an estimate
- Only a stereo output

If a library sounds different from the old G-Player, the file will probably use one of these.
Tell me which instrument and what is different, and it can be added.

## How it was checked

- `Tests/engine_test.cpp` builds a small .gig file and checks pitch, loops, streaming
  (with no clicks at the hand-over from RAM to disk), velocity layers and release samples.
- The plugin was run through the *pluginval* checker (level 5) with no problems.
- Round robin, crossfades, key switches and controller dimensions are written but were not covered by that test.
- It has **not** been tested with your own Vienna files, because they were not available.

To run the engine test on Linux (needs the `libgig-dev` package):

```
g++ -std=c++17 -O2 -I/usr/include/libgig Source/GigEngine.cpp Tests/engine_test.cpp -o engine_test -lgig -lpthread
./engine_test
```

## Build it yourself

GitHub builds it for you (see `.github/workflows/build-windows.yml`).
To build on your own PC you need Visual Studio 2022 (with C++) and CMake:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target GigGoatPlayer_VST3
```

The plugin appears in `build\GigGoatPlayer_artefacts\Release\VST3\`.

## Files

- `Source/GigEngine.*` the sampler (no JUCE in it)
- `Source/PluginProcessor.*` the plugin part (MIDI in, audio out, saving state)
- `Source/PluginEditor.*` the window
- `Tests/engine_test.cpp` the engine test

## Licence

JUCE 8 is AGPLv3 and libgig is GPL. This project is meant for your own use. If you give the
plugin to other people, give them the source code too.
Your sample libraries have their own licence from their makers.

## Orchestra of Goats GiG Player

A second plugin with up to 16 load bays. Press **+ Add part** and the window grows by one bay
(it scrolls when there are many). Every bay has its own:

- **Open .gig** button, instrument list and arrows
- **Volume**, **Pan** and **Fine pitch** (+/- 100 cents). Double-click a knob to reset it
- **MIDI ch**: *All* plays the bay from every MIDI channel. Pick a channel (1-16) to make the
  bay answer only that channel, so one MIDI source can play a whole orchestra

All the knobs can be automated from the host. The song remembers every bay's file and instrument.
Each bay keeps its own sample RAM, so many big libraries use a lot of memory.
