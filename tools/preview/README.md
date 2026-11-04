# PC preview tools

These tools compile the firmware's renderer (`src/lyric_fx.c`, `src/fx_draw.c`, `src/scenes.c`) for your PC. You can see any moment of the lyric video as a PNG without flashing the board, and benchmark scenes against each other. The renderer has no ESP-IDF dependencies, so the PC build produces the same frames as the device.

## One-time setup

You need any C compiler and Python with Pillow. The easiest compiler on Windows is Zig, installed from pip:

```powershell
python -m pip install ziglang pillow
```

## Render a contact sheet

Run from the repository root:

```powershell
# build the preview tool
python -m ziglang cc -O2 -std=c11 -I include tools/preview/preview.c src/lyric_fx.c src/fx_draw.c src/scenes.c -o preview.exe

# render frames at song times in milliseconds (here: title, book, POW!, superhero, drop, eclipse)
./preview.exe frames.bin 4000 9800 21500 37500 48300 91800

# turn them into a PNG, labelled with the same times
python tools/preview/make_sheet.py frames.bin sheet.png 4000 9800 21500 37500 48300 91800
```

Open `sheet.png`. Each frame is shown at 2x. To find a line's time, look up its `start_ms` in `include/lyrics_data.h`, then add a second or so to see it mid-animation.

## Benchmark scenes

```powershell
python -m ziglang cc -O2 -ffast-math -std=c11 -I include tools/preview/bench.c src/lyric_fx.c src/fx_draw.c src/scenes.c -o bench.exe
./bench.exe
```

This prints the average µs per frame for one moment of each scene. A PC is very roughly 100–150x faster than the ESP32-S3, so treat the numbers as relative. They show which scene is heaviest and whether a change helped. For real timings, read the `fps | draw ... | finish ...` line the firmware prints on the serial monitor.
