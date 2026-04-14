#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_SUPPORT_ZLIB
#include "stb_image.h"

#define QBGX_VERSION 1

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

char* check = "QBGX";
unsigned char *fptr;
FILE *bgxf = NULL;

bool INTERNAL_colorExists(uint32_t* array, uint32_t color, uint32_t max_entries ){
    uint32_t temp;
    for (int k = 0; k<=max_entries; k++){
        temp = array[k];
        if (temp == color) return true;
    }
    return false;
}

uint8_t INTERNAL_GetIndex(uint32_t* array, uint32_t color, uint32_t max_entries ){
    uint32_t temp;
    for (int k = 0; k<=max_entries; k++){
        temp = array[k];
        if (temp == color) return k;
    }
    return false;
}

int encode(int x, int y, int n, unsigned char *in_data, FILE* out){
    uint32_t file_length = x*y*4;
    uint32_t color_value;
    memset(prevcolors, 0, sizeof(prevcolors));
    uint32_t color_value_prev = 0xFFFFFFF;
    
    uint32_t colors = 0;
    color_value = in_data[3];
    color_value |= (in_data[0] << 24);
    color_value |= (in_data[1] << 16);
    color_value |= (in_data[2] << 8);
    
    color_value_prev = color_value;
    prevcolors[colors] = color_value;

    printf(
        "Input file info:\n"
        "Width: %u\n"
        "Height: %u\n"
        "Depth: %u\n"
        "File array Length: %u KiB\n"
        "First color: #%06X\n",
        x,y,n, (file_length >> 10), color_value
        );    

    for (int i = 0; i < file_length; i+=4){
        color_value = in_data[i+3];
        color_value |= (in_data[i] << 24);
        color_value |= (in_data[i+1] << 16);
        color_value |= (in_data[i+2] << 8);
        if (color_value_prev != color_value){
            if (!INTERNAL_colorExists(prevcolors, color_value, colors)){
                if (colors == 256){
                    printf("bgx supports only up to 256 colors\nFor higher bitdepths use a different format.\n");
                    return -1;
                }
                colors++;
                color_value_prev = color_value;
                prevcolors[colors] = color_value;
            }
            
        }
    }

    printf("Total Unique Colors: %u\n", colors+1);
    for(int i = 0; i<=colors; i++){
        printf("   #%08X",prevcolors[i]);
        if (i%4 == 0 && i != 0) putchar('\n');
    }
    putchar('\n');
    bgx_header_t new_header;
    memset(&new_header, 0, sizeof(new_header));
    printf("Creating header...\n");
    memcpy(&new_header.magic, check, sizeof(new_header.magic));
    new_header.width = x;
    new_header.height = y;
    new_header.bpp = 1;
    if (colors > 1) new_header.bpp = 2;
    if (colors > 3) new_header.bpp = 4;
    if (colors > 15) new_header.bpp = 8;
    new_header.palette_size = colors;
    new_header.version = QBGX_VERSION;
    if (encode_RLE) new_header.flags = FLAG_RLE;
    fwrite(&new_header, sizeof(new_header), 1, out);
    printf("Writing palette...\n");
    for (int i = 0; i <= colors; i++){
        fwrite(&(prevcolors[i]), sizeof(uint32_t), 1, out);
    }
    printf("Writing pixels...\n");

    
    uint8_t repeat = 0;
    uint8_t previous_cell = 0;
    bool first_cell = true;

    uint8_t cell = 0;
    uint8_t written = 0;
    for (int i = 0; i < file_length; i+=4){
        color_value = in_data[i+3];
        color_value |= (in_data[i] << 24);
        color_value |= (in_data[i+1] << 16);
        color_value |= (in_data[i+2] << 8);
        
        uint8_t index = INTERNAL_GetIndex(prevcolors, color_value, colors);
        
        switch(new_header.bpp){
            case 1:
                cell |= (index << 7-written);
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
                cell |= (index << 6-(2*(written)));
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
                cell |= (index << 4-(4*(written)));
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
                printf("Unsupported Bitdepth\n");
                return -1; 
        }
    }
    printf("Header+Palette size: %u Bytes\n", sizeof(bgx_header_t)+((colors+1)*4));
}

int decode(FILE *data, unsigned char* out_path){
    char magic[5];
    bgx_header_t header;
    fread(&header, sizeof(header), 1, data);

    memcpy(magic, &header.magic, 4);
    magic[4] = 0;


    printf("File Info:\n");
    if (strcmp(magic, check) == 0){
        printf("Version: %u\n", header.version);
        printf("Width x Height: %ux%u\n", header.width, header.height);
        printf("Bit depth: %u bit\n", header.bpp);
        printf("Flags: 0x%X\n", header.flags);
        printf("Palette size: %u, Colors:\n", header.palette_size+1);
        memset(prevcolors, 0, sizeof(prevcolors));
        for (int i = 0; i <= header.palette_size; i++ ){
            uint32_t temp;
            fread(&temp, sizeof(uint32_t), 1, data);
            prevcolors[i] = temp;
            if (i%4 == 0 && i != 0) printf("   #%08X\n", temp);
        }
    } else {
        printf("not a BGX!\n");
        return -1;
    }

    printf("Decoding...\n");
    uint32_t* RGBA_Buffer = malloc(sizeof(uint32_t)*header.width*header.height);
    if (RGBA_Buffer == NULL){
        printf("Memory Alloc error.\n");
        return -1;
    }
    printf("Header+Palette size: %u Bytes\n", sizeof(bgx_header_t)+(header.palette_size)*4);

    uint32_t pixel_count = header.width*header.height;
    uint32_t cell_count = 0;
    if (header.bpp == 1) cell_count = 8;
    if (header.bpp == 2) cell_count = 4;
    if (header.bpp == 4) cell_count = 2;
    if (header.bpp == 8) cell_count = 1;
    if (cell_count == 0){
        printf("Unsupported bitdepth\n");
        return -1;
    }
    uint8_t cell;
    uint8_t cell_repeat;
    uint64_t pixels = 0;
    if (header.flags & FLAG_RLE){
        while (pixels<header.width*header.height){
            fread(&cell_repeat, sizeof(uint8_t), 1, data);
            fread(&cell, sizeof(uint8_t), 1, data);
            uint8_t pixel_pos, index;
            for (int i = 0;i<=cell_repeat*cell_count+cell_count;i++){
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
                    printf("Unsupported bitdepth\n");
                    return -1;  
                }
                RGBA_Buffer[pixels+i-cell_count] = prevcolors[index] >> 24;
                RGBA_Buffer[pixels+i-cell_count] |= (prevcolors[index] & 0x00FF0000) >> 8;
                RGBA_Buffer[pixels+i-cell_count] |= (prevcolors[index] & 0x0000FF00) << 8;
                RGBA_Buffer[pixels+i-cell_count] |= (prevcolors[index] & 0x000000FF) << 24;
            }
            pixels+=(cell_repeat*cell_count)+cell_count;
        }
    } else {
        for (int i = 0; i<pixel_count; i++){    
        if (i%cell_count == 0) fread(&cell, sizeof(uint8_t), 1, data);
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
                printf("Unsupported bitdepth\n");
                return -1;  
        }
        
        RGBA_Buffer[i] = prevcolors[index] >> 24;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x00FF0000) >> 8;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x0000FF00) << 8;
        RGBA_Buffer[i] |= (prevcolors[index] & 0x000000FF) << 24;
    }

    
    }

    stbi_write_png(out_path, header.width, header.height, 4, (uint8_t*)RGBA_Buffer, header.width*4);

    free(RGBA_Buffer);
    return 0;
}

int main(int argc, char *argv[]){
    
    printf("BGX decoder test\n");

    int x,y,n;

    
    if (argc <= 2){
        printf("BootGraphics file software\n"
               "%s [mode] [input] [output]\n",
               argv[0]
        );
        return -1;
    }

    for (int i = 0; i<argc; i++){
        if (strcmp(argv[i],"rle") == 0){
            encode_RLE = true;
        }
    }
    
    switch (argv[1][0]){
        case 'e':
            if (argc > 3){
                fptr = stbi_load(argv[2], &x, &y, &n, 4);
                bgxf = fopen(argv[3], "w");
                if (fptr == NULL){
                    printf("bgx file read error\n");
                    return -1;
                }
                if (bgxf == NULL){
                    printf("bgx file read error\n");
                    return -1;
                }
            } else {
                printf("Not enough args\n");
                return -1;
            }
            encode(x,y,n,fptr,bgxf);
            fclose(bgxf);
            break;
        case 'd':
            if (argc > 3){
                bgxf = fopen(argv[2], "r");
                if (bgxf == NULL){
                    printf("bgx file read error\n");
                    return -1;
                }
                decode(bgxf, argv[3]);
                fclose(bgxf);
            } else {
                printf("Not enough args\n");
                return -1;
            }
            break;
        default:
            printf("BootGraphics file software\n"
               "%s [mode] [input] [output]\n",
               argv[0]
            );
            return -1;
    }
    stbi_image_free(fptr);
    return 0;

}


