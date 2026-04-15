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
#define QBGX_FORMAT_VERSION 1
#define QBGX_SOFTWARE_VERSION "1.1"


typedef struct bgx_header{
    uint32_t magic; // "QBGX"
    uint8_t version; // 1
    uint16_t width, height;
    uint8_t bpp; // 1-bit, 2-bit, 4-bit, maybe 8-bit depth if not expensive
    uint8_t flags; // 0x1 will be RLE enable and RLE is meant to be in all bitdepths
    uint8_t palette_size;
} __attribute__((packed)) bgx_header_t;
#define FLAG_RLE 0x1

uint32_t prevcolors[256];

bool encode_RLE = false;
bool quiet = false;

char* check = "QBGX";
stbi_uc *fptr;
FILE *bgxf = NULL;

void ShowHelp(char* bgx_filename){
    if (!quiet) printf("Command Syntax:\n"
            "%s [mode] [input] [output] [additional argument]\n"
            "Available Modes:\n"
            " - d (Decode - Convert a .bgx file to .png with automatic settings)\n"
            " - e (Encode - Convert a PNG, GIF, BMP or TGA file to .png with automatic settings)\n"
            " - i (Info - Shows information about the .bgx file)\n"
            "Available Arguments:\n"
            " - rle (Applies run length encoding on encoded bytes)\n"
            " - q (Quiet - Refrains from printing unless errors occur)\n",
            bgx_filename
    );
    return;
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

uint8_t INTERNAL_GetIndex(uint32_t* array, uint32_t color, uint32_t max_entries ){
    uint32_t temp;
    for (uint32_t k = 0; k<=max_entries; k++){
        temp = array[k];
        if (temp == color) return k;
    }
    return false;
}

uint32_t encode(uint32_t x, uint32_t y, uint32_t n, stbi_uc *in_data, FILE* out){
    if ( x > UINT16_MAX || y > UINT16_MAX){
        fprintf(stderr, "[ERROR] Image size too big!\n");
        return -1;
    }
    uint32_t file_length = x*y*4;
    uint32_t color_value;
    memset(prevcolors, 0, sizeof(prevcolors));
    uint32_t color_value_prev = 0xFFFFFFF;
    
    uint32_t palette_size = 0;
    color_value = in_data[3];
    color_value |= (in_data[0] << 24);
    color_value |= (in_data[1] << 16);
    color_value |= (in_data[2] << 8);
    
    color_value_prev = color_value;
    prevcolors[palette_size] = color_value;

    if (!quiet) printf(
        "Input file info:\n"
        "Width: %u\n"
        "Height: %u\n"
        "Depth: %u\n"
        "File array Length: %u KiB\n"
        "First color: #%06X\n",
        x,y,n, (file_length >> 10), color_value
        );    

    for (uint32_t i = 0; i < file_length; i+=4){
        color_value = in_data[i+3];
        color_value |= (in_data[i] << 24);
        color_value |= (in_data[i+1] << 16);
        color_value |= (in_data[i+2] << 8);
        if (color_value_prev != color_value){
            if (!INTERNAL_colorExists(prevcolors, color_value, palette_size)){
                if (palette_size == 256){
                    fprintf(stderr, "[ERROR] BGX supports only up to 256 colors\n        For higher bitdepths use a different format.\n");
                    return -1;
                }
                palette_size++;
                color_value_prev = color_value;
                prevcolors[palette_size] = color_value;
            }
            
        }
    }

    if (!quiet) printf("Total Unique Colors: %u\n", palette_size+1);
    for(uint32_t i = 0; i<=palette_size; i++){
        if (!quiet) printf("   #%08X",prevcolors[i]);
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
    if (palette_size > 1) new_header.bpp = 2;
    if (palette_size > 3) new_header.bpp = 4;
    if (palette_size > 15) new_header.bpp = 8;
    new_header.palette_size = palette_size;
    new_header.version = QBGX_FORMAT_VERSION;
    if (encode_RLE) new_header.flags = FLAG_RLE;
    fwrite(&new_header, sizeof(new_header), 1, out);
    if (!quiet) printf("Writing palette...\n");
    for (uint32_t i = 0; i <= palette_size; i++){
        fwrite(&(prevcolors[i]), sizeof(uint32_t), 1, out);
        
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
        
        uint8_t index = INTERNAL_GetIndex(prevcolors, color_value, palette_size);
        
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
        if (!quiet) printf("Version: %u\n", header.version);
        if (!quiet) printf("Width x Height: %ux%u\n", header.width, header.height);
        if (!quiet) printf("Bit depth: %u bit\n", header.bpp);
        if (!quiet) printf("Flags: 0x%X, Which mean:\n", header.flags);
        if (header.flags == 0) if (!quiet) printf("  This is a normal BGX File.\n");
        if (header.flags & 0x1) if (!quiet) printf("  This BGX file uses RLE for compression.\n");
        if (!quiet) printf("Palette size: %u, Colors:\n", header.palette_size+1);
        memset(prevcolors, 0, sizeof(prevcolors));
        for (uint32_t i = 0; i <= header.palette_size; i++ ){
            uint32_t temp;
            fread(&temp, sizeof(uint32_t), 1, data);
            prevcolors[i] = temp;
            if (!quiet) printf("   #%08X", temp);
            if ((i+1)%4 == 0) if (!quiet) putchar('\n');
        }
        if (!quiet) putchar('\n');
    } else {
        fprintf(stderr, "[ERROR] This is not a BGX file!\n");
        return -1;
    }
    if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size)*4));
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
        if (!quiet) printf("Version: %u\n", header.version);
        if (!quiet) printf("Width x Height: %ux%u\n", header.width, header.height);
        if (!quiet) printf("Bit depth: %u bit\n", header.bpp);
        if (!quiet) printf("Flags: 0x%X, Which mean:\n", header.flags);
        if (header.flags == 0) if (!quiet) printf("  This is a normal BGX File.\n");
        if (header.flags & 0x1) if (!quiet) printf("  This BGX file uses RLE for compression.\n");
        if (!quiet) printf("Palette size: %u, Colors:\n", header.palette_size+1);
        memset(prevcolors, 0, sizeof(prevcolors));
        for (uint32_t i = 0; i <= header.palette_size; i++ ){
            uint32_t temp;
            fread(&temp, sizeof(uint32_t), 1, data);
            prevcolors[i] = temp;
            if (!quiet) printf("   #%08X", temp);
            if ((i+1)%4 == 0) if (!quiet) putchar('\n');
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
    if (!quiet) printf("Header+Palette size: %u Bytes\n", (uint32_t)(sizeof(bgx_header_t)+(header.palette_size)*4));

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
    uint8_t cell;
    uint8_t cell_repeat;
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
                        if (!quiet) printf("[WARNING] Index out of bounds on cell %u, found value 0x%02X.   \n          File may be corrupted. Replaced with color index #0\n", (uint32_t)cells,   index);
                        return -1;
                    }
                    if (cells + i + k >= header.width*header.height) {
                        printf("[WARNING] RLE decode overflow, skipping the rest (%s)\n", out_path);
                        goto skip_rle;
                    }
                    RGBA_Buffer[cells+i+k] = prevcolors[index] >> 24;
                    RGBA_Buffer[cells+i+k] |= (prevcolors[index] & 0x00FF0000) >> 8;
                    RGBA_Buffer[cells+i+k] |= (prevcolors[index] & 0x0000FF00) << 8;
                    RGBA_Buffer[cells+i+k] |= (prevcolors[index] & 0x000000FF) << 24;
                }
            }
            if (cells != 0) cells+=(cell_repeat+1) * pixels_in_cell;
            else cells+=cell_repeat * pixels_in_cell;
            
        }
    } else {
        for (uint32_t i = 0; i<pixel_countA; i++){    
        if (i%pixels_in_cell == 0) fread(&cell, sizeof(uint8_t), 1, data);
        uint8_t pixel_pos, index;
        switch (header.bpp){
            case 1:
                pixel_pos = 7-((i%8));
                index = (cell >> pixel_pos) & 0x1;
                break;
            case 2:
                pixel_pos = 6-((i%4)*2);
                index = (cell >> pixel_pos) & 0x3;
                break;
            case 4:
                pixel_pos = 4-((i%2)*4);
                index = (cell >> pixel_pos) & 0xF;
                break;
            case 8:
                index = cell;
                break;
            default:
                fprintf(stderr, "[ERROR] Unsupported bitdepth\n");
                return -1;  
        }
        
        RGBA_Buffer[i] = prevcolors[index] >> 24;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x00FF0000) >> 8;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x0000FF00) << 8;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x000000FF) << 24;
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
        if (strcmp(argv[i],"rle") == 0){
            encode_RLE = true;
        }
        if (strcmp(argv[i],"q") == 0){
            quiet = true;
        }
    }

    if (!quiet) printf("BootGraphics Converison tool\n"
           "Version: %s\n",
           QBGX_SOFTWARE_VERSION
    );
    
    switch (argv[1][0]){
        case 'e':
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
    return 0;

}


