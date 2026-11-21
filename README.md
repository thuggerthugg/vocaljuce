# VoxDose All-in-One - vocal plugin (JUCE)

Everything for a vocal in one window:

- **DOSE** (big knob): compression, mud cut, presence, air and saturation, all at once
- **Gate**: cuts background noise between phrases (Off at 0)
- **Tune**: auto-tune with **Key** + **Scale** and **Retune speed** (Instant = hard tune). Off at 0
- **Tone**: darker <-> brighter, **De-ess**: tames harsh "s" sounds
- **Width**: vocal doubler / stereo spread, **Delay** + **Echo time** (ping-pong), **Reverb**
- **Dry/Wet** and **Output**, plus 8 presets (Clean Vocal, Rap Punch, Airy Pop, Dark Plug, Hard Tune Plug, Spacey Main, Max Dose)

Signal flow: gate -> tuner -> high-pass -> compressor -> EQ -> saturation -> de-esser -> width -> delay -> reverb -> mix.

## Get the .vst3 and .exe WITHOUT installing anything (GitHub builds it for you)
1. Make a free account at github.com and create a **New repository** (private is fine).
2. Click **uploading an existing file** and drag in everything from this folder, including the
   hidden `.github` folder. If `.github` does not upload, click **Add file > Create new file**,
   type `.github/workflows/build.yml` as the name and paste in that file's contents.
3. Open the **Actions** tab, wait a few minutes for **Build for Windows** to finish (green check).
4. Open the finished run and download **VoxDose-Windows** under *Artifacts*:
   - `VST3\VoxDose.vst3` -> copy into `C:\Program Files\Common Files\VST3\`, then in FL Studio
     use **Options > Manage plugins > Find installed plugins**
   - `Standalone\VoxDose.exe` -> a normal app to double-click and test

## Notes
- The plugin always adds about 35 ms of delay (for the tuner) and reports it to FL Studio so it
  can compensate. Tune works on one voice at a time.
- This code was written without being compiled or tested on my side. If the build shows a red X,
  copy the first error lines and send them back so they can be fixed.
- JUCE is free for personal use; check its license before selling or sharing the plugin.
