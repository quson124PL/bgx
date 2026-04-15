# BGX - BootGraphics image format
Used for an osdev project
<hr>

# Features:
- Uses indexed palettes
- 1,2,4,8 bit per pixel raw encoding.
- Per-byte RLE
- Supports 32-bit RGBA, (WIP) ~~6-bit VGA DAC and EGA Attribute colors for the palette.~~
<hr>

# How to use:
### Command Syntax:
```bgx [mode] [input] [output] [additional argument]```
### Available Modes:
 - ```d``` (Decode - Convert a .bgx file to .png with automatic settings)
 - ```e``` (Encode - Convert a PNG, GIF, BMP or TGA file to .png with automatic settings)
 - ```i``` (Info - Shows information about the .bgx file)
### Available Arguments:
 - ```rle``` (Applies run length encoding on encoded bytes)
 - ```q``` (Quiet - Refrains from printing unless errors occur)

## License

This project is licensed under the Mozilla Public License 2.0 (MPL-2.0).

See the LICENSE file for details.
