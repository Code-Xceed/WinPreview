# WinPreview MP3 & Audio Test Suite

This directory contains pristine, high-fidelity sample audio files designed to test WinPreview's native Media Foundation engine, Direct2D audio visualizer, album art extractor, and interactive playback controls.

---

## Included Sample Tracks

| File Name | Title | Artist | Style / Genre | Highlights |
| :--- | :--- | :--- | :--- | :--- |
| **`01_Synthwave_Night_Drive.mp3`** | *Synthwave Night Drive* | Neon Runner | 80s Synthwave / Electronic | Punchy 4-on-the-floor kick, snappy snare, driving rolling bassline & stereo synth pads. |
| **`02_Acoustic_Morning_Chords.mp3`** | *Acoustic Morning Chords* | Elena Woods | Acoustic / Folk Arpeggio | Warm plucked string harmonics, G-C-Em-D chord progression with wide stereo imaging. |
| **`03_LoFi_Chill_Beats.mp3`** | *Midnight Study Session* | Coffee Cup Collective | Lo-Fi / Hip-Hop | Mellow Rhodes electric piano 7th chords with tremolo, vinyl crackle, and boom-bap rhythm. |
| **`04_Cyberpunk_Bass_Pulse.mp3`** | *Neural Overdrive* | Sector 7 | Industrial / Cyberpunk | Heavy distorted sub-bass modulation with dynamic panning and aggressive electronic tone. |
| **`05_Crystal_Notification_Chime.mp3`** | *Crystal Interface Chime* | WinPreview Sounds | UI Sound Effects / Chime | Pristine bell chime harmonics (E6, G#6, B6, E7) with long reverberant decay. |
| **`cover.jpg`** | *WinPreview Audio* | Design Studio | 600×600 Hi-Res Album Art | Dark-glass metallic album cover art embedded into the ID3v2 tags. |

---

## Features to Test in WinPreview

1. **Spacebar Quick Look**: Select any `.mp3` in Windows Explorer and press **Spacebar** to immediately preview the track.
2. **Interactive Scrubber**: Click or drag anywhere along the blue progress scrubber bar to seek to any point in the song.
3. **28-Bar Spectrum Equalizer**: Live audio frequency bars dynamically animate in real-time at 60/120/144 FPS.
4. **Rotating Vinyl vs. Album Art**:
   - If album art is present (`cover.jpg` or embedded ID3v2 metadata), WinPreview displays the card with rounded corners and soft drop shadows.
   - If an audio file has no cover, it renders a procedural spinning vinyl record with concentric grooves, center label, and glowing music note.
5. **Playback Controls**:
   - **Play / Pause**: Center hero circular button or **Spacebar**.
   - **Seek -10s / +10s**: Skip buttons or **Left / Right Arrow keys**.
   - **Looping**: Toggle the repeat button or press **L** on your keyboard.
   - **Mute / Unmute**: Click the volume speaker icon or press **M**.
