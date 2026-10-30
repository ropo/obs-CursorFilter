# OBS Cursor Filter

English | [日本語](README-ja.md)

An OBS Studio effect filter for Windows that draws the Windows mouse cursor over a Game Capture source. It follows the Windows pointer size setting and can use a configured `.cur` file to preserve the detail of custom cursors.

Requires OBS Studio 33 on Windows.

## Installation

With OBS closed, place the `obs-cursor-filter` plugin folder in `C:\ProgramData\obs-studio\plugins`. The folder must contain `obs-cursor-filter.dll` and `data\locale`. Start OBS after installation.

For portable OBS, place the folder in the `plugins` folder inside the portable OBS directory instead.

## Usage

1. Turn off **Capture Cursor** in your Game Capture source.
2. Open the source's **Filters** and add **Cursor Capture** under Effect Filters.
3. If Game Capture is set to capture a specific window, the filter uses that window by default. Otherwise, turn off **Use the Game Capture window** and select a window manually.
4. Check the cursor's position and size in the OBS preview. Adjust **Size adjustment** if needed; `1.0` follows the Windows size setting.

Automatic window selection matches the configured title, class, and executable. If the game changes its window title or OBS matches it differently, select the window manually. The filter can show the cursor only while the selected window is active.

The filter assumes the captured image covers the window's entire client area. Some fullscreen modes, display scaling, or different game resolutions may cause a position mismatch. Cursors drawn inside the game image cannot be removed by this filter. HDR and multiple monitors with different DPI settings have not been verified.

## License

GPL-2.0-or-later. See [COPYING](COPYING).
