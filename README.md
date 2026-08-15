# BGX - BootGraphics image format
Used for an osdev project


## Features:
- Uses indexed palettes
- 1,2,4,8 bit per pixel encoding.
- Per-byte RLE
- Supports 32-bit RGBA, 6-bit VGA DAC, VGA Mode 13h and EGA Attribute colors for the palette.


## How to use:
### Command Syntax:
```bgx [mode] [input] [output] [additional arguments]```
### Available Modes:
 - ```d``` (Decode - Convert a .bgx file to .png with automatic settings)
 - ```e``` (Encode - Convert a PNG, GIF, BMP or TGA file to .bgx with automatic settings)
 - ```i``` (Info - Shows information about the .bgx file)
### Available Arguments:
 - ```rle``` (Applies run length encoding on encoded bytes)
 - ```q``` (Quiet - Refrains from printing unless errors occur)
### Available Arguments (palette type, only one allowed):
 - ```vga``` (Default Mode 13h VGA Palette, stores no palette in a file, always 8 bpp.)
 - ```vgadac``` (6-bit per channel VGA DAC Value, stored as 3 uint8_t's)
 - ```ega``` (2-bit per channel EGA ATC DAC Value, stored as a single uint8_t. In order to make your image displayable on EGA make sure to use only up to 16 colors.)

No palette type arguments will make the converter encode in RGBA32.


## License

This project is licensed under the Mozilla Public License 2.0 (MPL-2.0).

See the LICENSE file for details.
