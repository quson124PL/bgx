/*
 * Copyright (c) 2026 quson
 *
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_write.h"
#pragma GCC diagnostic pop
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR

// Supporting predictable formats.
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA

#define STBI_NO_LINEAR
#define STBI_SUPPORT_ZLIB
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop
#define QBGX_FORMAT_VERSION 2
#define QBGX_SOFTWARE_VERSION "1.2"


typedef struct bgx_header{
    uint32_t magic; // "QBGX"
    uint8_t version;
    uint16_t width, height;
    uint8_t bpp; // 1-bit, 2-bit, 4-bit, maybe 8-bit depth if not expensive
    uint8_t flags; // 0x1 will be RLE enable and RLE is meant to be in all bitdepths
    uint8_t palette_size;
    uint8_t transparent_index; // Default is 0xFF for no transparent colors. Useful only in non RGBA color modes.
} __attribute__((packed)) bgx_header_t;


#define FLAG_RLE 0x1

/*
    Special palettes
    Used for storing EGA/VGA DAC information without additional conversion cost.

    0b00000XY0
    XY - 2 bits for defining the palette

    00 - RGBA 32-bit palette (default)
    01 - Default Mode 13h VGA Palette
    10 - 6-bit per channel VGA DAC Value, stored as 3 uint8_t's
    11 - 2-bit per channel EGA ATC DAC Value, stored as a single uint8_t.
         In order to make your image displayable at all on EGA make sure to use only up to 16 colors.
*/

#define FLAG_VGA_PAL 0x2
#define FLAG_VGA_DAC 0x4
#define FLAG_EGA_ATC 0x6

// HASHmap FUNCTIONS
typedef struct {
    uint32_t key;
    uint8_t value;
    uint8_t used;
} hashmap_entry_t;

#define HASHMAP_SIZE 512  // must be > max colors (256)
hashmap_entry_t palmap[HASHMAP_SIZE];

static inline uint32_t hash(uint32_t key) {
    return key * 2654435761u; // Knuth multiplicative hash
}

void hashmap_put(uint32_t key, uint8_t value, hashmap_entry_t* map) {
    uint32_t idx = hash(key) % HASHMAP_SIZE;

    while (map[idx].used) {
        if (map[idx].key == key) {
            return; // already exists
        }
        idx = (idx + 1) % HASHMAP_SIZE; // linear probing
    }

    map[idx].used = 1;
    map[idx].key = key;
    map[idx].value = value;
}

int hashmap_get(uint32_t key, uint8_t* out, hashmap_entry_t* map) {
    uint32_t idx = hash(key) % HASHMAP_SIZE;

    while (map[idx].used) {
        if (map[idx].key == key) {
            *out = map[idx].value;
            return 1; // found
        }
        idx = (idx + 1) % HASHMAP_SIZE;
    }

    return 0; // not found
}



// END OF hashmap FUNCTIONS

typedef struct dacColor {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} __attribute__((packed)) dacColor_t;
extern const dacColor_t VGAint13hPalette[];
const size_t VGAint13hPaletteSize;

//uint32_t palette[256];
void* palette;

bool encode_RLE = false;
#define COLTYPE_RGBA32 0
#define COLTYPE_VGADAC 1
#define COLTYPE_VGAPAL 2
#define COLTYPE_EGAATC 3
uint8_t colorType = COLTYPE_RGBA32;

bool quiet = false;
char* check = "QBGX";
stbi_uc *fptr;
FILE *bgxf = NULL;

void INTERNAL_convertRGBAtoVGADAC(uint32_t color, dacColor_t* output){

	// little endian
	output->red = (((color & 0x0000FF00) >> 8)/4);
	output->green = (((color & 0x00FF0000) >> 16)/4);
	output->blue = (((color & 0xFF000000) >> 24)/4);
};

void INTERNAL_convertVGADACtoRGBA(const dacColor_t* color, uint32_t* output){
    *output = 0xFF;
    *output |= ((color->blue*4) & 0xFF) << 24;
    *output |= ((color->green*4) & 0xFF) << 16;
    *output |= ((color->red*4) & 0xFF) << 8;
};

void INTERNAL_convertRGBAtoEGAATC(uint32_t color, uint8_t* output){
    // 0b--RRGGBB
    *output = (((color & 0x0000FF00) >> 8)/64); // more or less 3
	*output |= ((((color & 0x00FF0000) >> 16)/64) << 2 );
	*output |= ((((color & 0xFF000000) >> 24)/64) << 4 );
}

void INTERNAL_convertEGAATCtoRGBA(uint8_t color, uint32_t* output){
    *output = 0xFF;
    *output |= (((color & 0b00110000) >> 4)*85) << 24;
    *output |= (((color & 0b00001100) >> 2)*85) << 16;
    *output |= (((color & 0b00000011))*85) << 8;
}

void ShowHelp(char* bgx_filename){
    if (!quiet) printf("Command Syntax:\n"
            "%s [mode] [input] [output] [additional arguments]\n"
            "Available Modes:\n"
            " - d (Decode - Convert a .bgx file to .png with automatic settings)\n"
            " - e (Encode - Convert a PNG, GIF, BMP or TGA file to .png with automatic settings)\n"
            " - i (Info - Shows information about the .bgx file)\n"
            "Available Arguments (generic):\n"
            " - rle (Applies run length encoding on encoded bytes)\n"
            " - q (Quiet - Refrains from printing unless errors occur)\n"
            "Available Arguments (palette type, only one allowed):\n"
            " - vga (Default Mode 13h VGA Palette)\n"
            " - vgadac (6-bit per channel VGA DAC Value, stored as 3 uint8_t's)\n"
            " - ega (2-bit per channel EGA ATC DAC Value, stored as a single uint8_t.\n        In order to make your image displayable on EGA make sure to use only up to 16 colors.)\n",
            bgx_filename
    );
    // THIS WILL BE ADDED LATER , 8 Additional VGA DAC colors may be supplied to replace the unused slots.
    return;
}

void ShowInfo(bgx_header_t* header){
    if (!quiet) printf("Version: %u\n", header->version);
    if (!quiet) printf("Width x Height: %ux%u\n", header->width, header->height);
    if (!quiet) printf("Bit depth: %u bit\n", header->bpp);
    if (!quiet) printf("Flags: 0x%X, Which mean:\n", header->flags);
    if (header->flags == 0) if (!quiet) printf("  This is a normal BGX File.\n");
    if (header->flags & FLAG_RLE) if (!quiet) printf("  This BGX file uses RLE for compression.\n");
    if (header->version >= 2){
        if (!quiet) printf("  Palette colors are defined as");
        if (!quiet){
            switch ((header->flags & 0b110)){
                case FLAG_VGA_PAL:
                    printf(" a mode 13h default palette index (248 colors, 8 unused)\n");
                    break;
                case FLAG_VGA_DAC:
                    printf(" VGA DAC values (~262k colors total, 256 at one time)\n");
                    break;
                case FLAG_EGA_ATC:
                    printf(" EGA ATC values (64 distinct colors)\n");
                    break;
                default:
                    printf(" a 32-bit RGBA value\n");
                    break;
        }
    }
    if (!quiet && header->transparent_index != 0xFF) printf("Transparent color at index: %u\n", header->transparent_index);
    }
    if ((header->flags & 0b110) == FLAG_VGA_PAL){
        if (!quiet) printf("Additional Colors: %lu, All values:\n", (size_t)(header->palette_size+1-VGAint13hPaletteSize));
    } else {
        if (!quiet) printf("Palette size: %u, Colors:\n", header->palette_size+1);
    }
    

}

bool INTERNAL_colorExists(uint32_t* array, uint32_t color, uint32_t max_entries ){
    uint32_t temp;
    for (uint32_t k = 0; k<=max_entries; k++){
        temp = array[k];
        if (temp == color) return true;
    } 
    return false;
}

uint8_t INTERNAL_ExtractIndex(uint8_t cell, uint8_t pixels_per_cell, uint8_t i){
    uint8_t pixel_pos;
    switch (pixels_per_cell){
        case 8:
            pixel_pos = 7-((i%8));
            return (cell >> pixel_pos) & 0x1;
        case 4:
            pixel_pos = 6-((i%4)*2);
            return (cell >> pixel_pos) & 0x3;
        case 2:
            pixel_pos = 4-((i%2)*4);
            return (cell >> pixel_pos) & 0xF;
        case 1:
            return cell;
        default:
            fprintf(stderr, "[ERROR] Unsupported bitdepth\n");
            return -1;  
        }
}

uint8_t INTERNAL_GetIndex(void* array, uint32_t color, uint32_t max_entries, uint8_t arraytype, uint8_t transparent_index){
    if (((color & 0xFF) == 0) && (arraytype == COLTYPE_VGADAC)){
        return 254;
    }
    uint32_t temp;
    for (uint32_t k = 0; k<=max_entries; k++){
    switch (arraytype){

        case COLTYPE_RGBA32:
            temp = ((uint32_t*)array)[k];
            if (temp == color) return k;
            break;
        case COLTYPE_VGAPAL:
        case COLTYPE_VGADAC:
            INTERNAL_convertVGADACtoRGBA( &(((dacColor_t*)array)[k]), &temp);
            if (temp == color) return k;
            break;
        case COLTYPE_EGAATC:
            INTERNAL_convertEGAATCtoRGBA(((uint8_t*)array)[k], &temp);
            if (k == transparent_index) temp = (temp & ~0xFF);
            if (temp == color) return k;
            break;
        default:
            fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
            return -1;
        }
    
    }
    return false;
}



/* 
    Finds closest color in palette, returns the amount of quantizations when succeeeded or -1 for failure.
    For use with COLTYPE_VGAPAL and other palette modes that have predefined colors.

    ENCODE ONLY
*/
uint32_t INTERNAL_FindClosestMatchingIndex(uint32_t color_value, uint8_t* index){

    dacColor_t buffer;
    uint32_t min_difference = UINT16_MAX, best_index = 0;
    INTERNAL_convertRGBAtoVGADAC(color_value, &buffer);

    for(size_t i = 0; i < VGAint13hPaletteSize; i++){
        uint32_t current_difference = (uint32_t)
        1000*sqrtf(
            (buffer.red - VGAint13hPalette[i].red)*(buffer.red - VGAint13hPalette[i].red)+
            (buffer.green - VGAint13hPalette[i].green)*(buffer.green - VGAint13hPalette[i].green)+
            (buffer.blue - VGAint13hPalette[i].blue)*(buffer.blue - VGAint13hPalette[i].blue)
        );

        if (current_difference < min_difference){
            min_difference = current_difference;
            best_index = i;
        }
    };
    *index = best_index;
    return (uint16_t)min_difference;
}

void INTERNAL_InsertLEColor32(uint8_t index, uint32_t* dest){
    switch (colorType){
        case COLTYPE_RGBA32:
            *dest = ((uint32_t*)palette)[index] >> 24;
            *dest |= (((uint32_t*)palette)[index] & 0x00FF0000) >> 8;
            *dest |= (((uint32_t*)palette)[index] & 0x0000FF00) << 8;
            *dest |= (((uint32_t*)palette)[index] & 0x000000FF) << 24;
            break;
        case COLTYPE_VGAPAL:
        case COLTYPE_VGADAC:
            uint32_t output;
            INTERNAL_convertVGADACtoRGBA(&(((dacColor_t*)palette)[index]), &output);
            *dest = output >> 24;
            *dest |= (output & 0x00FF0000) >> 8;
            *dest |= (output & 0x0000FF00) << 8;
            *dest |= (output & 0x000000FF) << 24;
            break;
        case COLTYPE_EGAATC:
            uint32_t temp;
            INTERNAL_convertEGAATCtoRGBA(((uint8_t*)palette)[index], &temp);
            *dest = temp >> 24;
            *dest |= (temp & 0x00FF0000) >> 8;
            *dest |= (temp & 0x0000FF00) << 8;
            *dest |= (temp & 0x000000FF) << 24;
            break;
        default:
            fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
    }
    
}

uint32_t encode(uint32_t x, uint32_t y, uint32_t n, stbi_uc *in_data, FILE* out){
    if ( x > UINT16_MAX || y > UINT16_MAX){
        fprintf(stderr, "[ERROR] Image size too big!\n");
        return -1;
    }
    uint32_t file_length = x*y*4;
    uint32_t color_value;
    dacColor_t dac_color_value = {0};
    uint8_t ega_color_value;
    uint8_t transparent_choice = 0xFF; // 0xFF is default.
    memset(palmap, 0, sizeof(palmap));
    
    uint32_t palette_size = 0;
    uint32_t old_value = 0;
    color_value = in_data[3];
    color_value |= (in_data[0] << 24);
    color_value |= (in_data[1] << 16);
    color_value |= (in_data[2] << 8);

    if (colorType == COLTYPE_VGAPAL || colorType == COLTYPE_VGADAC){
        INTERNAL_convertRGBAtoVGADAC(color_value, &dac_color_value);
    }

    if (colorType == COLTYPE_EGAATC){
        INTERNAL_convertRGBAtoEGAATC(color_value, &ega_color_value);
    }

    if (!quiet) {
            printf(
            "Input file info:\n"
            "Width: %u\n"
            "Height: %u\n"
            "Depth: %u\n"
            "File array Length: %u KiB\n",
            x,y,n, (file_length >> 10)
            );
        switch (colorType){
        case COLTYPE_EGAATC:
            printf("First color: 0x%02X\n", ega_color_value);
            break;
        case COLTYPE_RGBA32:
            printf("First color: #%06X\n", color_value);
            break;
        // VGAPAL's first color is constant.
        case COLTYPE_VGADAC:
            printf("First color: (%02u, %02u, %02u)\n",
                    dac_color_value.red,
                    dac_color_value.green,
                    dac_color_value.blue);
            break;
        default:
            break;
    }
    }

    // There are colors already defined.
    if (colorType == COLTYPE_VGAPAL ){
        if (!quiet) printf("   Setting up the mode 13h palette...\n");
        uint32_t VGA13hImportRGBA = 0;
        //palette_size = VGAint13hPaletteSize;
        for (size_t i = 0; i < VGAint13hPaletteSize; i++){
            INTERNAL_convertVGADACtoRGBA(&(VGAint13hPalette[i]), &VGA13hImportRGBA);
            ((dacColor_t*)palette)[i].red = VGAint13hPalette[i].red;
            ((dacColor_t*)palette)[i].green = VGAint13hPalette[i].green;
            ((dacColor_t*)palette)[i].blue = VGAint13hPalette[i].blue;
        }
        palette_size = VGAint13hPaletteSize;
    }

    for (uint32_t i = 0; i < file_length; i+=4){
        color_value = in_data[i+3];
        color_value |= (in_data[i] << 24);
        color_value |= (in_data[i+1] << 16);
        color_value |= (in_data[i+2] << 8);
        if (colorType == COLTYPE_VGAPAL || colorType == COLTYPE_VGADAC)INTERNAL_convertRGBAtoVGADAC(color_value, &dac_color_value);
        if (colorType == COLTYPE_EGAATC)INTERNAL_convertRGBAtoEGAATC(color_value, &ega_color_value);

    
    uint8_t index = 0;
    switch (colorType){
        case COLTYPE_RGBA32:
            if (!hashmap_get(color_value, &index, palmap)) {
                if (palette_size == 256){
                    fprintf(stderr, "[ERROR] BGX supports only up to 256 colors\n        For higher bitdepths use a different format.\n");
                    return -1;
                }
                index = palette_size;
                ((uint32_t*)palette)[palette_size] = color_value;
                hashmap_put(color_value, index, palmap);
                palette_size++;
                if (transparent_choice == 0xFF && (color_value & 0xFF) == 0x0){
                    transparent_choice = index;
                }
            }            
            break;
        case COLTYPE_VGAPAL:
            old_value = color_value;
            INTERNAL_convertVGADACtoRGBA(&dac_color_value, &color_value);
            color_value = (color_value & ~0xFF) | (old_value & 0xFF);
            if (!hashmap_get(color_value, &index, palmap)) {
                uint16_t quants = INTERNAL_FindClosestMatchingIndex(color_value, &index);
                if (transparent_choice == 0xFF && (color_value & 0xFF) == 0x0){
                    transparent_choice = 16;
                    index = 16;
                }
                if (!quiet) printf("  Color #%08X is different by %u than index %u\n",    color_value, quants, index);
                hashmap_put(color_value, index, palmap);
            }  
            break;
        case COLTYPE_VGADAC:
            old_value = color_value;
            INTERNAL_convertVGADACtoRGBA(&dac_color_value, &color_value);
            color_value = (color_value & ~0xFF) | (old_value & 0xFF);
            if ((color_value & 0xFF) == 0){
                transparent_choice = 254;
                index = 254;
                break;
            }
            if (!hashmap_get(color_value, &index, palmap)) {
                if (palette_size == 256){
                    fprintf(stderr, "[ERROR] BGX supports only up to 256 colors\n        For higher bitdepths use a different format.\n");
                    return -1;
                }
                index = palette_size;
                ((dacColor_t*)palette)[palette_size].red = dac_color_value.red;
                ((dacColor_t*)palette)[palette_size].green = dac_color_value.green;
                ((dacColor_t*)palette)[palette_size].blue = dac_color_value.blue;
                hashmap_put(color_value, index, palmap);
                palette_size++;
            }  
            break;
        case COLTYPE_EGAATC:
            old_value = color_value;
            INTERNAL_convertEGAATCtoRGBA(ega_color_value, &color_value);
            color_value = (color_value & ~0xFF) | (old_value & 0xFF);
            if (!hashmap_get(color_value, &index, palmap)) {
                index = palette_size;
                ((uint8_t*)palette)[palette_size] = ega_color_value;
                palette_size++;
                if (transparent_choice == 0xFF && (color_value & 0xFF) == 0x0){
                    transparent_choice = index;
                }
                hashmap_put(color_value, index, palmap);
            }      
            break; 
        default:
            fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
            return -1;
    }
    }
    if (colorType == COLTYPE_EGAATC && palette_size > 15){
        fprintf(stderr, "[WARNING] Counted %u colors in the image, that is too many for real EGA\n", palette_size);
    }
    


    if (!quiet) printf("Total Unique Colors: %u\n", palette_size);
    for(uint32_t i = 0; i<palette_size; i++){
        switch (colorType){
            case COLTYPE_RGBA32:
            if (!quiet) printf("   #%08X",((uint32_t*)palette)[i]);
                break;
            case COLTYPE_VGAPAL:
            if (!quiet) printf("   (%02u, %02u, %02u),",
                    (((dacColor_t*)palette)[i].red),
                    (((dacColor_t*)palette)[i].green),
                    (((dacColor_t*)palette)[i].blue));
                break;
            case COLTYPE_VGADAC:
            if (!quiet) printf("   (%02u, %02u, %02u),",
                    (((dacColor_t*)palette)[i].red),
                    (((dacColor_t*)palette)[i].green),
                    (((dacColor_t*)palette)[i].blue));
            break;
            case COLTYPE_EGAATC:
                if (!quiet) printf("   0x%02X",((uint8_t*)palette)[i]);
                break;
            default:
                fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
                return -1;
            }
        
        if ((i+1)%4 == 0) if (!quiet) putchar('\n');
    }
    if (!quiet) putchar('\n');

    

    bgx_header_t new_header;
    memset(&new_header, 0, sizeof(new_header));
    if (!quiet) printf("Creating header...\n");
    memcpy(&new_header.magic, check, sizeof(new_header.magic));
    new_header.width = x;
    new_header.height = y;
    new_header.bpp = 1;
    new_header.transparent_index = transparent_choice;
    if (palette_size > 1) new_header.bpp = 2;
    if (palette_size > 3) new_header.bpp = 4;
    if (palette_size > 15) new_header.bpp = 8;
    new_header.palette_size = palette_size-1;
    new_header.version = QBGX_FORMAT_VERSION;
    if (encode_RLE) new_header.flags = FLAG_RLE;
    switch (colorType){
        case COLTYPE_VGAPAL:
            new_header.flags |= FLAG_VGA_PAL;
            break;
        case COLTYPE_VGADAC:
            new_header.flags |= FLAG_VGA_DAC;
            break;
        case COLTYPE_EGAATC:
            new_header.flags |= FLAG_EGA_ATC;
            break; 
        default:
            break;
    }
    fwrite(&new_header, sizeof(new_header), 1, out);
    if (!quiet) printf("Writing palette...\n");

    uint32_t paload_i = 0;
    if (colorType == COLTYPE_VGAPAL){
        paload_i = VGAint13hPaletteSize;
        new_header.palette_size = VGAint13hPaletteSize;
    } else {
        paload_i = 0;
    }
    for (; paload_i < palette_size; paload_i++){
        switch (colorType){
            case COLTYPE_RGBA32:
                fwrite(&(((uint32_t*)palette)[paload_i]), sizeof(uint32_t), 1, out);
                break;
            case COLTYPE_VGAPAL:
                // fwrite(&(((dacColor_t*)palette)[VGAint13hPaletteSize+paload_i]), sizeof(dacColor_t), 1, out);
                break;
            case COLTYPE_VGADAC:
                fwrite(&(((dacColor_t*)palette)[paload_i]), sizeof(dacColor_t), 1, out);
                break;
            case COLTYPE_EGAATC:
                fwrite(&(((uint8_t*)palette)[paload_i]), sizeof(uint8_t), 1, out);
                break; 
            default:
                fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
                return -1;
        }
    }
    if (!quiet) printf("Writing cells...\n");

    uint8_t repeat = 0;
    uint8_t previous_cell = 0;
    bool first_cell = true;

    uint8_t cell = 0;
    uint8_t written = 0;
    for (uint32_t i = 0; i < file_length; i+=4){
        color_value = in_data[i+3];
        color_value |= (in_data[i] << 24);
        color_value |= (in_data[i+1] << 16);
        color_value |= (in_data[i+2] << 8);

        if (colorType == COLTYPE_VGADAC || colorType == COLTYPE_VGAPAL){
            uint8_t save_alpha = color_value & 0xFF;
            dacColor_t tempcolor;
            INTERNAL_convertRGBAtoVGADAC(color_value, &tempcolor);
            INTERNAL_convertVGADACtoRGBA(&tempcolor, &color_value);
            color_value = (color_value & ~0xFF) | save_alpha;
        }

        if (colorType == COLTYPE_EGAATC){
            uint8_t save_alpha = color_value & 0xFF;
            uint8_t tempcolorEGA;
            INTERNAL_convertRGBAtoEGAATC(color_value, &tempcolorEGA);
            INTERNAL_convertEGAATCtoRGBA(tempcolorEGA, &color_value);
            color_value = (color_value & ~0xFF) | save_alpha;
        }
        
        uint8_t index;

        
        if (colorType == COLTYPE_VGAPAL){
            if ((color_value & 0xFF) != 0){
                INTERNAL_FindClosestMatchingIndex(color_value, &index);    
            } else {
                index = 16;
            }
        } else index = INTERNAL_GetIndex(palette, color_value, palette_size, colorType, transparent_choice);
        
        // if (colorType == COLTYPE_VGADAC && index == 0xF){
        //     printf("[WARN] DUMP: 0x%02X, 0x%08X, %u, %u\n", index, color_value, palette_size, transparent_choice);
        // }

        switch(new_header.bpp){
            case 1:
                cell |= (index << (7-written));
                written++;
                if (written == 8){
                    if (encode_RLE){
                        if ((previous_cell != cell || repeat == 255) && !first_cell){
                            fwrite(&repeat, sizeof(uint8_t), 1 , out);
                            fwrite(&previous_cell, sizeof(uint8_t), 1, out);
                            previous_cell = cell;
                            repeat = 0;
                        } else {
                            first_cell = false;
                            repeat++;
                        }
                    } else {
                        fwrite(&cell, sizeof(uint8_t), 1, out);
                    }
                    written = 0;
                    cell = 0;
                }
                break;
            case 2:
                cell |= (index << (6 - 2*(written)));
                written++;
                if (written == 4){
                    if (encode_RLE){
                        if ((previous_cell != cell || repeat == 255) && !first_cell){
                            fwrite(&repeat, sizeof(uint8_t), 1 , out);
                            fwrite(&previous_cell, sizeof(uint8_t), 1, out);
                            previous_cell = cell;
                            repeat = 0;
                        } else {
                            first_cell = false;
                            repeat++;
                        }
                    } else {
                        fwrite(&cell, sizeof(uint8_t), 1, out);
                    }
                    written = 0;
                    cell = 0;
                }
                break;
            case 4:
                cell |= (index << (4 - 4*(written)));
                written++;
                if (written == 2){
                    if (encode_RLE){
                        if ((previous_cell != cell || repeat == 255) && !first_cell){
                            fwrite(&repeat, sizeof(uint8_t), 1 , out);
                            fwrite(&previous_cell, sizeof(uint8_t), 1, out);
                            previous_cell = cell;
                            repeat = 0;
                        } else {
                            first_cell = false;
                            repeat++;
                        }
                    } else {
                        fwrite(&cell, sizeof(uint8_t), 1, out);
                    }
                    written = 0;
                    cell = 0;
                }
                break;
            case 8:
                if (encode_RLE){
                    if ((previous_cell != index || repeat == 255) && !first_cell){
                        fwrite(&repeat, sizeof(uint8_t), 1 , out);
                        fwrite(&previous_cell, sizeof(uint8_t), 1, out);
                        previous_cell = index;
                        repeat = 0;
                    } else {
                        first_cell = false;
                        repeat++;
                    }
                } else {
                    fwrite(&index, sizeof(uint8_t), 1, out);
                }
                break;
            default:
                fprintf(stderr, "[ERROR] Unsupported Bitdepth\n");
                return -1; 
        }
    }
    if (encode_RLE && !first_cell) {
        fwrite(&repeat, sizeof(uint8_t), 1 , out);
        fwrite(&previous_cell, sizeof(uint8_t), 1, out);
    }
    if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+((palette_size+1)*4)));
    if (!quiet) printf("[SUCCESS] Encoding successful!\n");
    return 0;
}
uint32_t info(FILE* data){
    char magic[5];
    bgx_header_t header;
    fread(&header, sizeof(header), 1, data);

    memcpy(magic, &header.magic, 4);
    magic[4] = 0;

    if (!quiet) printf("File Info:\n");
    if (strcmp(magic, check) == 0){
        
        ShowInfo(&header);
        
        if (header.version == 1){
            fseek(data, -sizeof(uint8_t), SEEK_CUR);
        }
        for (uint32_t i = 0; i <= header.palette_size; i++ ){
            uint32_t temp;
            dacColor_t tempdac;
            uint8_t tempEGA;
            switch (header.flags & 0b110){
                case FLAG_VGA_PAL:
                    INTERNAL_convertVGADACtoRGBA(&(VGAint13hPalette[i]), &temp);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
                case FLAG_VGA_DAC:
                    fread(&tempdac, sizeof(dacColor_t), 1, data);
                    INTERNAL_convertVGADACtoRGBA(&tempdac, &temp);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
                case FLAG_EGA_ATC:
                    fread(&tempEGA, sizeof(dacColor_t), 1, data);
                    INTERNAL_convertEGAATCtoRGBA(tempEGA, &temp);
                    ((uint8_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break; 
                default:
                    fread(&temp, sizeof(uint32_t), 1, data);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
            }
        }
        if (!quiet) putchar('\n');
    } else {
        fprintf(stderr, "[ERROR] This is not a BGX file!\n");
        return -1;
    }
    if ((header.flags & 0b110) == FLAG_VGA_PAL) {
        if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size-VGAint13hPaletteSize+1)*4));
    } else {
        if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size)*4));
    }
    
    return 0;
}

uint32_t decode(FILE *data, char* out_path){
    char magic[5];
    bgx_header_t header;
    fread(&header, sizeof(header), 1, data);

    memcpy(magic, &header.magic, 4);
    magic[4] = 0;


    if (!quiet) printf("File Info:\n");
    if (strcmp(magic, check) == 0){
        if ((header.flags & 0b110) == FLAG_VGA_PAL) header.palette_size = VGAint13hPaletteSize-1;
        ShowInfo(&header);
        if (header.version == 1){
            fseek(data, -sizeof(uint8_t), SEEK_CUR);
        }
        
        for (uint32_t i = 0; i <= header.palette_size; i++ ){
            uint32_t temp;
            dacColor_t tempdac;
            uint8_t tempEGA;
            switch (header.flags & 0b110){
                case FLAG_VGA_PAL:
                    INTERNAL_convertVGADACtoRGBA(&(VGAint13hPalette[i]), &temp);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
                case FLAG_VGA_DAC:
                    fread(&tempdac, sizeof(dacColor_t), 1, data);
                    INTERNAL_convertVGADACtoRGBA(&tempdac, &temp);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
                case FLAG_EGA_ATC:
                    fread(&tempEGA, sizeof(uint8_t), 1, data);
                    INTERNAL_convertEGAATCtoRGBA(tempEGA, &temp);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break; 
                default:
                    fread(&temp, sizeof(uint32_t), 1, data);
                    ((uint32_t*)palette)[i] = temp;
                    if (!quiet) printf("   #%08X", temp);
                    if ((i+1)%4 == 0) if (!quiet) putchar('\n');
                    break;
            }
        }
        if (!quiet) putchar('\n');
    } else {
        fprintf(stderr, "[ERROR] This is not a BGX file!\n");
        return -1;
    }

    if (header.version > QBGX_FORMAT_VERSION){
        fprintf(stderr, "[ERROR] Unsupported version. Please download a newer version of the tool.\n");
        if (!quiet) printf("        This software version (%s) supports only file versions up to %u\n",
        QBGX_SOFTWARE_VERSION, QBGX_FORMAT_VERSION);
        return -1;
    }

    if (!quiet) printf("Decoding...\n");
    uint32_t* RGBA_Buffer = malloc(sizeof(uint32_t)*header.width*header.height);
    if (RGBA_Buffer == NULL){
        fprintf(stderr, "[ERROR] Couldn't allocate memory for decoding.\n");
        return -1;
    }
    if ((header.flags & 0b110) == FLAG_VGA_PAL) {
        if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size-VGAint13hPaletteSize+1)*4));
    } else {
        if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size)*4));
    }

    uint32_t pixel_countA = header.width*header.height;
    uint32_t pixels_in_cell = 0;
    if (header.bpp == 1) pixels_in_cell = 8;
    if (header.bpp == 2) pixels_in_cell = 4;
    if (header.bpp == 4) pixels_in_cell = 2;
    if (header.bpp == 8) pixels_in_cell = 1;
    if (pixels_in_cell == 0){
        fprintf(stderr, "[ERROR] Unsupported bitdepth\n");
        return -1;
    }
    uint8_t cell, cell_repeat;
    uint64_t cells = 0;
    if (header.flags & FLAG_RLE){
        while (cells<header.width*header.height){
            fread(&cell_repeat, sizeof(uint8_t), 1, data);
            fread(&cell, sizeof(uint8_t), 1, data);
            uint8_t index;
            for (uint32_t i = 0;i<=cell_repeat*pixels_in_cell;i+=pixels_in_cell){
                for (uint8_t k = 0; k<pixels_in_cell; k++){
                    index = INTERNAL_ExtractIndex(cell, pixels_in_cell, k);
                    if (index > header.palette_size){
                        if ((header.flags & 0b110) != FLAG_VGA_DAC){
                            fprintf(stderr, "[WARNING] Index out of bounds on cell %u, found value 0x%02X despite only having %u colors.   \n          File may be corrupted. Replaced with color index #0\n          (%s)\n", (uint32_t)cells, index, header.palette_size+1, out_path);
                        }
                    }
                    if (cells + i + k >= header.width*header.height) {
                        fprintf(stderr, "[WARNING] RLE decode overflow, skipping the rest (%s)\n", out_path);
                        goto skip_rle;
                    }
                    RGBA_Buffer[cells+i+k] = ((uint32_t*)palette)[index] >> 24;
                    RGBA_Buffer[cells+i+k] |= (((uint32_t*)palette)[index] & 0x00FF0000) >> 8;
                    RGBA_Buffer[cells+i+k] |= (((uint32_t*)palette)[index] & 0x0000FF00) << 8;
                    RGBA_Buffer[cells+i+k] |= (((uint32_t*)palette)[index] & 0x000000FF) << 24;
                    if (index == header.transparent_index) RGBA_Buffer[cells+i+k] &= 0x00FFFFFF;
                }
            }
            if (cells != 0) cells+=(cell_repeat+1) * pixels_in_cell;
            else cells+=cell_repeat * pixels_in_cell;
            
        }
    } else {
        for (uint32_t i = 0; i<pixel_countA; i++){    
        if (i%pixels_in_cell == 0) fread(&cell, sizeof(uint8_t), 1, data);
        uint8_t index;
        index = INTERNAL_ExtractIndex(cell, pixels_in_cell, i);        
        RGBA_Buffer[i] = ((uint32_t*)palette)[index] >> 24;
        RGBA_Buffer[i] |= (((uint32_t*)palette)[index] & 0x00FF0000) >> 8;
        RGBA_Buffer[i] |= (((uint32_t*)palette)[index] & 0x0000FF00) << 8;
        RGBA_Buffer[i] |= (((uint32_t*)palette)[index] & 0x000000FF) << 24;
        if (index == header.transparent_index) RGBA_Buffer[i] &= 0x00FFFFFF;
    }

    
    }
    skip_rle:
    stbi_write_png(out_path, header.width, header.height, 4, (uint8_t*)RGBA_Buffer, header.width*4);

    free(RGBA_Buffer);
    if (!quiet) printf("[SUCCESS] Decoding successful! (Output: %s)\n", out_path);
    return 0;
}

int main(int argc, char *argv[]){
    

    int32_t x,y,n;

    
    if (argc <= 2){
        ShowHelp(argv[0]);
        return -1;
    }

    for (int i = 0; i<argc; i++){
        if (strcmp(argv[i],"q") == 0){
            quiet = true;
        }
        if (strcmp(argv[i],"rle") == 0){
            encode_RLE = true;
        }
        if (strcmp(argv[i],"vgadac") == 0){
            if (colorType == COLTYPE_RGBA32) colorType = COLTYPE_VGADAC;
            else {
                fprintf(stderr, "[ERROR] Conflicting palette arguments. (%s)\n", argv[i]);
                if (!quiet) ShowHelp(argv[0]);
                return -1;
            }
        }
        if (strcmp(argv[i],"vga") == 0){
            if (colorType == COLTYPE_RGBA32) colorType = COLTYPE_VGAPAL;
            else {
                fprintf(stderr, "[ERROR] Conflicting palette arguments. (%s)\n", argv[i]);
                if (!quiet) ShowHelp(argv[0]);
                return -1;
            }
        }
        if (strcmp(argv[i],"ega") == 0){
            if (colorType == COLTYPE_RGBA32) colorType = COLTYPE_EGAATC;
            else {
                fprintf(stderr, "[ERROR] Conflicting palette arguments. (%s)\n", argv[i]);
                if (!quiet) ShowHelp(argv[0]);
                return -1;
            }
        }
    }


    if (!quiet) printf("BootGraphics Converison tool\n"
           "Version: %s\n",
           QBGX_SOFTWARE_VERSION
    );
    
    switch (argv[1][0]){
        case 'e':
            switch (colorType){
                case COLTYPE_RGBA32:
                    palette = calloc(256,sizeof(uint32_t));
                    break;
                case COLTYPE_VGAPAL:
                    palette = calloc(256,sizeof(dacColor_t));
                    memcpy(palette, VGAint13hPalette, VGAint13hPaletteSize);
                    break;
                case COLTYPE_VGADAC:
                    palette = calloc(256,sizeof(dacColor_t));
                    break;
                case COLTYPE_EGAATC:
                    palette = calloc(64,sizeof(uint8_t));
                    break; 
                default:
                    fprintf(stderr, "[ERROR] Attempted to allocate size for an unknown palette.\n");
                    return -1;
            }
            if (argc > 3){
                fptr = stbi_load(argv[2], &x, &y, &n, 4);
                bgxf = fopen(argv[3], "wb");
                if (fptr == NULL){
                    fprintf(stderr, "[ERROR] Failed reading input file.\n");
                    return -1;
                }
                if (bgxf == NULL){
                    fprintf(stderr, "[ERROR] Failed reading output file.\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "[ERROR] Not enough args\n");
                ShowHelp(argv[0]);
                return -1;
            }
            encode(x,y,n,fptr,bgxf);
            fclose(bgxf);
            break;
        case 'd':
            palette = calloc(256,sizeof(uint32_t));
            if (argc > 3){
                bgxf = fopen(argv[2], "rb");
                if (bgxf == NULL){
                    fprintf(stderr, "[ERROR] Failed reading input file.\n");
                    return -1;
                }
                decode(bgxf, argv[3]);
                fclose(bgxf);
            } else {
                fprintf(stderr, "[ERROR] Not enough args\n");
                ShowHelp(argv[0]);
                return -1;
            }
            break;
        case 'i':
            palette = calloc(256,sizeof(uint32_t));
            if (argc > 2){
                bgxf = fopen(argv[2], "rb");
                if (bgxf == NULL){
                    fprintf(stderr, "[ERROR] Failed reading input file.\n");
                    return -1;
                }
                info(bgxf);
                fclose(bgxf);
            } else {
                fprintf(stderr, "[ERROR] Not enough args\n");
                ShowHelp(argv[0]);
                return -1;
            }
            break;
        default:
            ShowHelp(argv[0]);
            return -1;
    }
    stbi_image_free(fptr);
    free(palette);
    return 0;

}

// The last 8 are unused, bgx allows changing these
const dacColor_t VGAint13hPalette[] = {
	{0,0,0}, {0,0,42}, {0,42,0}, {0,42,42}, 
	{42,0,0}, {42,0,42}, {42,21,0}, {42,42,42}, 
	{21,21,21}, {21,21,63}, {21,63,21}, {21,63,63}, 
	{63,21,21}, {63,21,63}, {63,63,21}, {63,63,63}, 
	{0,0,0}, {4,4,4}, {8,8,8}, {13,13,13}, 
	{17,17,17}, {21,21,21}, {25,25,25}, {29,29,29}, 
	{34,34,34}, {38,38,38}, {42,42,42}, {46,46,46}, 
	{50,50,50}, {55,55,55}, {59,59,59}, {63,63,63}, 
	{0,0,63}, {16,0,63}, {32,0,63}, {47,0,63}, 
	{63,0,63}, {63,0,47}, {63,0,32}, {63,0,16}, 
	{63,0,0}, {63,16,0}, {63,32,0}, {63,47,0}, 
	{63,63,0}, {47,63,0}, {32,63,0}, {16,63,0}, 
	{0,63,0}, {0,63,16}, {0,63,32}, {0,63,47}, 
	{0,63,63}, {0,47,63}, {0,32,63}, {0,16,63}, 
	{32,32,63}, {39,32,63}, {47,32,63}, {55,32,63}, 
	{63,32,63}, {63,32,55}, {63,32,47}, {63,32,39}, 
	{63,32,32}, {63,39,32}, {63,47,32}, {63,55,32}, 
	{63,63,32}, {55,63,32}, {47,63,32}, {39,63,32}, 
	{32,63,32}, {32,63,39}, {32,63,47}, {32,63,55}, 
	{32,63,63}, {32,55,63}, {32,47,63}, {32,39,63}, 
	{46,46,63}, {50,46,63}, {55,46,63}, {59,46,63}, 
	{63,46,63}, {63,46,59}, {63,46,55}, {63,46,50}, 
	{63,46,46}, {63,50,46}, {63,55,46}, {63,59,46}, 
	{63,63,46}, {59,63,46}, {55,63,46}, {50,63,46}, 
	{46,63,46}, {46,63,50}, {46,63,55}, {46,63,59}, 
	{46,63,63}, {46,59,63}, {46,55,63}, {46,50,63}, 
	{0,0,28}, {7,0,28}, {14,0,28}, {21,0,28}, 
	{28,0,28}, {28,0,21}, {28,0,14}, {28,0,7}, 
	{28,0,0}, {28,7,0}, {28,14,0}, {28,21,0}, 
	{28,28,0}, {21,28,0}, {14,28,0}, {7,28,0}, 
	{0,28,0}, {0,28,7}, {0,28,14}, {0,28,21}, 
	{0,28,28}, {0,21,28}, {0,14,28}, {0,7,28}, 
	{14,14,28}, {17,14,28}, {21,14,28}, {24,14,28}, 
	{28,14,28}, {28,14,24}, {28,14,21}, {28,14,17}, 
	{28,14,14}, {28,17,14}, {28,21,14}, {28,24,14}, 
	{28,28,14}, {24,28,14}, {21,28,14}, {17,28,14}, 
	{14,28,14}, {14,28,17}, {14,28,21}, {14,28,24}, 
	{14,28,28}, {14,24,28}, {14,21,28}, {14,17,28}, 
	{20,20,28}, {22,20,28}, {24,20,28}, {26,20,28}, 
	{28,20,28}, {28,20,26}, {28,20,24}, {28,20,22}, 
	{28,20,20}, {28,22,20}, {28,24,20}, {28,26,20}, 
	{28,28,20}, {26,28,20}, {24,28,20}, {22,28,20}, 
	{20,28,20}, {20,28,22}, {20,28,24}, {20,28,26}, 
	{20,28,28}, {20,26,28}, {20,24,28}, {20,22,28}, 
	{0,0,16}, {4,0,16}, {8,0,16}, {12,0,16}, 
	{16,0,16}, {16,0,12}, {16,0,8}, {16,0,4}, 
	{16,0,0}, {16,4,0}, {16,8,0}, {16,12,0}, 
	{16,16,0}, {12,16,0}, {8,16,0}, {4,16,0}, 
	{0,16,0}, {0,16,4}, {0,16,8}, {0,16,12}, 
	{0,16,16}, {0,12,16}, {0,8,16}, {0,4,16}, 
	{8,8,16}, {10,8,16}, {12,8,16}, {14,8,16}, 
	{16,8,16}, {16,8,14}, {16,8,12}, {16,8,10}, 
	{16,8,8}, {16,10,8}, {16,12,8}, {16,14,8}, 
	{16,16,8}, {14,16,8}, {12,16,8}, {10,16,8}, 
	{8,16,8}, {8,16,10}, {8,16,12}, {8,16,14}, 
	{8,16,16}, {8,14,16}, {8,12,16}, {8,10,16}, 
	{11,11,16}, {12,11,16}, {13,11,16}, {15,11,16}, 
	{16,11,16}, {16,11,15}, {16,11,13}, {16,11,12}, 
	{16,11,11}, {16,12,11}, {16,13,11}, {16,15,11}, 
	{16,16,11}, {15,16,11}, {13,16,11}, {12,16,11}, 
	{11,16,11}, {11,16,12}, {11,16,13}, {11,16,15}, 
	{11,16,16}, {11,15,16}, {11,13,16}, {11,12,16}, 
};

const size_t VGAint13hPaletteSize =
    sizeof(VGAint13hPalette) / sizeof(VGAint13hPalette[0]);

/*
switch (colorType){
        case COLTYPE_RGBA32:
            break;
        case COLTYPE_VGAPAL:
            break;
        case COLTYPE_VGADAC:
            break;
        case COLTYPE_EGAATC:
            break; 
        default:
            fprintf(stderr, "[ERROR] Attempted to process an unknown palette.\n");
            return -1;
    }

*/