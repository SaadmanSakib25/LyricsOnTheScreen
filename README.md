# Lyrics on the Screen: "Something Just Like This"

An aesthetic, synchronized lyrical display for the **128x160 ST7735 SPI TFT display** running on the **ESP32-S3**, featuring full lyrics for **"Something Just Like This"** by **Coldplay & The Chainsmokers**.

---

## 📌 Hardware Wiring

| Signal | ST7735 Pin | ESP32-S3 GPIO | Description |
|---|---|---|---|
| **MOSI / SDA** | SDA / SDI | **GPIO 38** | SPI Data out |
| **SCLK** | SCK / CLK | **GPIO 39** | SPI Clock |
| **CS** | CS | **GPIO 40** | Chip Select |
| **DC** | DC / A0 / RS | **GPIO 41** | Data / Command |
| **RST** | RES / RESET | **GPIO 42** | Hardware Reset |
| **BL** | BL / LED | **GPIO 47** | Backlight (PWM Dimming) |
| **VCC** | VCC | **3.3V** | Display Power (3.3V) |
| **GND** | GND | **GND** | Ground |

---

## ✨ Features

- **Full Song Lyrics & Timestamps**: Synchronized with the official 4:07 track of *"Something Just Like This"* (Coldplay x The Chainsmokers).
- **"Black-screen lyric video" look** (like the viral TikTok / Reels lyric edits), no player chrome:
  - **Only the line being sung** sits on a pure black screen, big and centered, auto-sized to fit.
  - **Words appear one by one as they're sung**, each line style animated differently:
    - *Verse*: words float up and fade in, cool blue glow.
    - *Typewriter*: letters type out with a blinking cursor.
    - *Chorus*: big words pop in with a pink neon glow.
    - *Drop* ("Doo-doo-doo"): words zoom in on the beat, colors cycle, shockwave rings and screen pulse on every beat, slight camera shake, and lines zoom out through the camera on exit.
    - *Title card*: song title builds up word by word, then the artist line fades in.
    - *Instrumental*: a music note breathes to the beat with a small caption.
  - **Soft neon glow** (blurred text halo) that flares on the word just sung.
  - **Accent words** (`*superhero*`, `*Batman*`, ...) in a highlight color.
  - Slow drifting dust particles in the background.
  - Smooth anti-aliased type: the 5x7 font is EPX-smoothed, proportionally spaced, and rendered at any scale.
- **Easy to retime / restyle**: every line in [include/lyrics_data.h](include/lyrics_data.h) has a start time and a style (`LS_VERSE`, `LS_TYPE`, `LS_CHORUS`, `LS_DROP`, `LS_TITLE`, `LS_BREAK`). Use `|` to force a line break and `*...*` for accent color. Set `SHOW_PROGRESS_LINE` to 1 in [src/lyric_fx.c](src/lyric_fx.c) if you want a hairline progress bar.
- **Interactive Controls (GPIO 0 BOOT Button & Serial)**:
  - **BOOT Button**:
    - Single short click: **Play / Pause** toggle.
    - Hold (>1.2s): **Restart from 00:00**.
  - **Serial Monitor (115200 baud)**:
    - `[Space]`: Play / Pause toggle.
    - `'r'` / `'R'`: Restart from beginning.
    - `'+'` / `'-'`: Nudge time forward / backward by 2 seconds (fine-tuning sync with phone audio).
    - `'0'`..`'9'`: Quick seek to 0%, 10%, 20%... of the song.

---

## 🚀 Build & Flash

Connect your ESP32-S3 via USB and execute:

```powershell
# Build firmware
pio run

# Flash to ESP32-S3
pio run --target upload

# Open Serial Monitor for real-time lyrics & playback controls
pio device monitor
```
