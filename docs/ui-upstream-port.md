# Desktop UI port

The launcher layout is a second development of the desktop UI in
[`STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop`](https://github.com/STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop).
That project is an ESP-IDF/LVGL 9 application built on ESP-Brookesia; this
firmware remains Arduino/LVGL 8 so the complete Brookesia runtime is not copied
into the build. The port keeps the source project's visual grammar: a fixed
status bar, four-column launcher, rounded app cards, page indicator, and one
active app page at a time.

The upstream Brookesia component is Apache-2.0. The original repository is kept
under `references/waveshare-desktop` for comparison only and is intentionally
not compiled or shipped as a vendored dependency.
