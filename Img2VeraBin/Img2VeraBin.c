#include <stdio.h>
#include <string.h>
#include <stdint.h>
// ---- STB_Image configs
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_FAILURE_USERMSG

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
}_sColorRGBA8;
typedef struct {
    uint8_t r : 4;
    uint8_t g : 4;
    uint8_t b : 4;
}_sColorRGB4;
typedef uint8_t _tColorIndexed;

int target_w = 0, target_h = 0;
typedef enum {
    BPP_INVALID = 0,
    BPP_1 = 1,
    BPP_2 = 2,
    BPP_4 = 4,
    BPP_8 = 8,
}_eBpp;
_eBpp target_bpp = BPP_INVALID;
enum {
    MODE_TILE,
    MODE_BITMAP
}output_mode;

_sColorRGBA8 palette[16][16] = { 0 };
int palette_size_x = 0, palette_size_y = 0;


void PrintHelp(int argn);

// asks for an exact match
// returns 1 if true 0 if false
// actual index is returned though a pointer
uint8_t IsColorInPalette(_sColorRGBA8 color, uint8_t palette_y, _eBpp palette_bpp, _tColorIndexed* index);
void GetClosestColorInPalette(_sColorRGBA8 color, uint8_t palette_y, _eBpp palette_bpp, _tColorIndexed* index, float* dist);
float GetColorDistance(_sColorRGBA8 c1, _sColorRGBA8 c2);
void PrintPaletteRGBA8(uint8_t palette_y);

void AutoBuildPalette(_sColorRGBA8* src, int w, int h);
void BuildPaletteFromImage(_sColorRGBA8* p_img, int img_w, int img_h);
_sColorRGBA8* ExtractImageSegment(_sColorRGBA8* src_img, int src_w, int src_h, int x, int y, int w, int h);
_tColorIndexed* ConvertRGBAtoIndexed(_sColorRGBA8* src_img, int img_w, int img_h, uint8_t palette_y, _eBpp palette_bpp, float* total_dist);
uint8_t* PackIndexedImage(_tColorIndexed* src_img, int* packed_size, int img_w, int img_h, _eBpp palette_bpp);


#define STRING_BUFFER_SIZE  400
char source_file_name[STRING_BUFFER_SIZE];
char output_file_name[STRING_BUFFER_SIZE];
char palette_file_name[STRING_BUFFER_SIZE];
enum {
    PALETTE_AUTO,
    PALETTE_USE_IMAGE,
}palette_mode = PALETTE_AUTO;


int main(int argc, char* argv[]) {
    int x_in, x_p, y_in, y_p, n, i, j, k;
    char c;
    // sting manipulation stuff
    char* ext_pos;
    // input images
    _sColorRGBA8* src_data;
    _sColorRGBA8* palette_data;
    // intermediates
    int tile_count_x, tile_count_y;
    _sColorRGBA8** tile_data_rgba;// array of images
    _tColorIndexed** tile_data_indexed;// array of images
    float temp_dist, best_dist; // used when picking best palette for low bpp tiles
    int best_palette;
    uint8_t** packed_tile_data;
    int* packed_tile_size;

    // ---- parse args
    if (strncmp(argv[1], "help", 4) == 0) {
        printf("\nhelp\n");
        PrintHelp(atoi(argv[2]));
        return 0;
    }
    if (argc < 5) {
        printf("\n could not parse parameters\n");
        PrintHelp(0);
        return 1; // Indicate error
    }

    strncpy(source_file_name, argv[1], strlen(argv[1]));
    source_file_name[STRING_BUFFER_SIZE - 1] = '\0';
    if (argc > 5) {
        strncpy(palette_file_name, argv[5], strlen(argv[5]));
        palette_file_name[STRING_BUFFER_SIZE - 1] = '\0';
        palette_mode = PALETTE_USE_IMAGE;
    }
    strncpy(output_file_name, source_file_name, strlen(argv[1]));
    output_file_name[STRING_BUFFER_SIZE - 1] = '\0';
    ext_pos = strrchr(output_file_name, '.');
    if (ext_pos) {
        *(ext_pos + 1) = 'B';
        *(ext_pos + 2) = 'I';
        *(ext_pos + 3) = 'N';
        *(ext_pos + 4) = '\0';
    } else {
        // no extension found, append one
        strncat(output_file_name, ".BIN", STRING_BUFFER_SIZE - strlen(output_file_name) - 1);
    }
    for (i = 0; i < STRING_BUFFER_SIZE; i++) {
        c = output_file_name[i];
        if (c >= 'a' && c <= 'z') {
            output_file_name[i] = c - ('a' - 'A');
        }
    }

    target_w = atoi(argv[2]);
    if (target_w == 320 || target_w == 640) {
        output_mode = MODE_BITMAP;
    } else if ((target_w == 8) || (target_w == 16) || (target_w == 32) || (target_w == 64)) {
        output_mode = MODE_TILE;
    } else {
        printf("invalid tile_width, valid options: 8 16 32 64 320 640");
        return 1;
    }
    target_h = atoi(argv[3]);
    if (output_mode == MODE_TILE) {
        if ((target_h != 8) && (target_h != 16) && (target_h != 32) && (target_h != 64)) {
            printf("invalid tile_height, valid options: 8 16 32 64");
            return 1;
        }
    } else if (target_h < 1) {
        printf("invalid tile_height");
        return 1;
    }

    target_bpp = atoi(argv[4]);
    if ((target_bpp != BPP_1) && (target_bpp != BPP_2) && (target_bpp != BPP_4) && (target_bpp != BPP_8)) {
        printf("invalid color_depth, valid options: 1 2 4 8");
        return 1;
    }

    //  ---- args are valid
    printf("loading: %s \n", source_file_name);
    src_data = (_sColorRGBA8*)stbi_load(source_file_name, &x_in, &y_in, &n, 4);
    if (!src_data) {
        // could not load file
        printf("%s \n", stbi_failure_reason());
        return 1;
    }
    if (palette_mode == PALETTE_USE_IMAGE) {
        printf("loading: %s \n", palette_file_name);
        palette_data = (_sColorRGBA8*)stbi_load(palette_file_name, &x_p, &y_p, &n, 4);
        if (!palette_data) {
            // could not load file
            printf("%s \n", stbi_failure_reason());
            return 1;
        }
    }

    printf("Source image size: %i x %i \n", x_in, y_in);



    /*  files loaded correctly, we can actually begin now */

    // make palette
    switch (palette_mode) {
    case PALETTE_USE_IMAGE:
        BuildPaletteFromImage(palette_data, x_p, y_p);
        printf("Colors from image: \n");
        for (i = 0; i < palette_size_y; i++) {
            PrintPaletteRGBA8(i);
        }
        break;

    case PALETTE_AUTO:
    default:
        AutoBuildPalette(src_data, x_in, y_in);
        printf("Autocolors: \n");
        for (i = 0; i < palette_size_y; i++) {
            PrintPaletteRGBA8(i);
        }
        break;
    }
    // palette ready

    // slice image into tiles / crop bitmap
    switch (output_mode) {
    case MODE_TILE:
        i = 0;
        tile_count_x = 0;
        while (i < x_in) {
            tile_count_x++;
            i += target_w;
        }
        i = 0;
        tile_count_y = 0;
        while (i < y_in) {
            tile_count_y++;
            i += target_h;
        }
        break;
    case MODE_BITMAP:
    default:
        tile_count_x = 1;
        tile_count_y = 1;
        break;
    }
    tile_data_rgba = malloc((sizeof(_sColorRGBA8*) * tile_count_x * tile_count_y));
    if (!tile_data_rgba) { return 1; }
    for (j = 0; j < tile_count_y; j++) {
        for (i = 0; i < tile_count_x; i++) {
            tile_data_rgba[i + (j * tile_count_x)] = ExtractImageSegment(src_data, x_in, y_in, i * target_w, j * target_h, target_w, target_h);
            if (!tile_data_rgba[i + (j * tile_count_x)]) { printf("could not allocate enough memory"); return 1; }
        }
    }

    // convert image segments to indexed colors
    tile_data_indexed = malloc((sizeof(_tColorIndexed*) * tile_count_x * tile_count_y));
    if (!tile_data_indexed) { return 1; }
    for (j = 0; j < tile_count_y; j++) {
        for (i = 0; i < tile_count_x; i++) {
            if (target_bpp == BPP_8) {
                // 8 bpp uses the one palette
                tile_data_indexed[i + (j * tile_count_x)] = ConvertRGBAtoIndexed(
                    tile_data_rgba[i + (j * tile_count_x)], target_w, target_h, 0, target_bpp, &temp_dist);
                if (!tile_data_indexed[i + (j * tile_count_x)]) { printf("could not allocate enough memory"); return 1; }
            } else {
                // loop to find best palette
                best_dist = INFINITY;
                best_palette = 0;
                for (k = 0; k < palette_size_y; k++) {
                    temp_dist = 0;
                    // this is very inneficient but idc, this doesn't have to run in real time or anything like that
                    tile_data_indexed[i + (j * tile_count_x)] = ConvertRGBAtoIndexed(
                        tile_data_rgba[i + (j * tile_count_x)], target_w, target_h, k, target_bpp, &temp_dist);
                    if (!tile_data_indexed[i + (j * tile_count_x)]) { printf("could not allocate enough memory"); return 1; }
                    free(tile_data_indexed[i + (j * tile_count_x)]);

                    if (temp_dist < best_dist) {
                        best_dist = temp_dist;
                        best_palette = k;
                    }
                }
                tile_data_indexed[i + (j * tile_count_x)] = ConvertRGBAtoIndexed(
                    tile_data_rgba[i + (j * tile_count_x)], target_w, target_h, best_palette, target_bpp, &temp_dist);
                if (!tile_data_indexed[i + (j * tile_count_x)]) { printf("could not allocate enough memory"); return 1; }
                printf("selected palette %i for tile %i %i\n", best_palette, i, j);
            }
            // done with this tile, we can free the RGBA data now
            free(tile_data_rgba[i + (j * tile_count_x)]);
        }
    }

    // pack the indexed data into bytes
    packed_tile_data = malloc((sizeof(_sColorRGBA8*) * tile_count_x * tile_count_y));
    if (!packed_tile_data) { return 1; }
    packed_tile_size = malloc((sizeof(int) * tile_count_x * tile_count_y));
    if (!packed_tile_size) { return 1; }

    for (j = 0; j < tile_count_y; j++) {
        for (i = 0; i < tile_count_x; i++) {
            packed_tile_data[i + (j * tile_count_x)] = PackIndexedImage(
                tile_data_indexed[i + (j * tile_count_x)], &packed_tile_size[i + (j * tile_count_x)], target_w, target_h, target_bpp);
            if (!packed_tile_data[i + (j * tile_count_x)]) { printf("could not allocate enough memory"); return 1; }
            // also done with this
            free(tile_data_indexed[i + (j * tile_count_x)]);
        }
    }

    // write output file (finally)
    printf("writing output file: %s \n", output_file_name);
    FILE* f = fopen(output_file_name, "wb");
    if (!f) { printf("could not create output file\n"); return 1; }

    for (j = 0; j < tile_count_y; j++) {
        for (i = 0; i < tile_count_x; i++) {
            fwrite(packed_tile_data[i + (j * tile_count_x)], sizeof(uint8_t), packed_tile_size[i + (j * tile_count_x)], f);
            free(packed_tile_data[i + (j * tile_count_x)]);
        }
    }
    fclose(f);
    printf("done!\n");
    return 0;
}

uint8_t* PackIndexedImage(_tColorIndexed* src_img, int* packed_size, int img_w, int img_h, _eBpp palette_bpp) {
    int i, j;
    uint8_t* data = 0;
    assert(src_img != 0);
    assert(img_w % 8 == 0); // width must be multiple of 8, height *could* be anything in bitmap mode but tiles are always multiple of 8

    switch (palette_bpp) {
    case BPP_1:
        *packed_size = (sizeof(uint8_t) * img_w * img_h) / 8;
        data = malloc((sizeof(uint8_t) * img_w * img_h) / 8);
        if (!data) { return 0; }
        for (j = 0; j < img_h; j++) {
            for (i = 0; i < img_w; i += 8) {
                // process 8 pixels at a time
                data[(i / 8) + (j * (img_w / 8))] =
                    ((src_img[i + (j * img_w)] & 0x01) << 7) |
                    ((src_img[(i + 1) + (j * img_w)] & 0x01) << 6) |
                    ((src_img[(i + 2) + (j * img_w)] & 0x01) << 5) |
                    ((src_img[(i + 3) + (j * img_w)] & 0x01) << 4) |
                    ((src_img[(i + 4) + (j * img_w)] & 0x01) << 3) |
                    ((src_img[(i + 5) + (j * img_w)] & 0x01) << 2) |
                    ((src_img[(i + 6) + (j * img_w)] & 0x01) << 1) |
                    ((src_img[(i + 7) + (j * img_w)] & 0x01) << 0);
            }
        }
        break;
    case BPP_2:
        *packed_size = (sizeof(uint8_t) * img_w * img_h) / 4;
        data = malloc((sizeof(uint8_t) * img_w * img_h) / 4);
        if (!data) { return 0; }
        for (j = 0; j < img_h; j++) {
            for (i = 0; i < img_w; i += 4) {
                // process 4 pixels at a time
                data[(i / 4) + (j * (img_w / 4))] =
                    ((src_img[i + (j * img_w)] & 0x03) << 6) |
                    ((src_img[(i + 1) + (j * img_w)] & 0x03) << 4) |
                    ((src_img[(i + 2) + (j * img_w)] & 0x03) << 2) |
                    ((src_img[(i + 3) + (j * img_w)] & 0x03) << 0);
            }
        }
        break;
    case BPP_4:
        *packed_size = (sizeof(uint8_t) * img_w * img_h) / 2;
        data = malloc((sizeof(uint8_t) * img_w * img_h) / 2);
        if (!data) { return 0; }
        for (j = 0; j < img_h; j++) {
            for (i = 0; i < img_w; i += 2) {
                // process 2 pixels at a time
                data[(i / 2) + (j * (img_w / 2))] =
                    ((src_img[i + (j * img_w)] & 0x0F) << 4) |
                    ((src_img[(i + 1) + (j * img_w)] & 0x0F) << 0);
            }
        }
        break;
    case BPP_8:
        *packed_size = (sizeof(uint8_t) * img_w * img_h);
        data = malloc(sizeof(uint8_t) * img_w * img_h);
        if (!data) { return 0; }
        for (j = 0; j < img_h; j++) {
            for (i = 0; i < img_w; i++) {
                // direct copy lol
                data[i + (j * img_w)] = src_img[i + (j * img_w)];
            }
        }
        break;

    default:
        return 0;
        break;
    }

    return data;
}

_tColorIndexed* ConvertRGBAtoIndexed(_sColorRGBA8* src_img, int img_w, int img_h, uint8_t palette_y, _eBpp palette_bpp, float* total_dist) {
    int i, j;
    _tColorIndexed* data = malloc(sizeof(_tColorIndexed) * img_w * img_h);
    _tColorIndexed index = 0;
    float dist = 0;
    assert(src_img != 0);

    if (!data) { return 0; }

    for (j = 0; j < img_h; j++) {
        for (i = 0; i < img_w; i++) {
            GetClosestColorInPalette(src_img[i + (j * img_w)], palette_y, palette_bpp, &index, &dist);
            *total_dist += dist;
            data[i + (j * img_w)] = index;
        }
    }
    return data;
}

_sColorRGBA8* ExtractImageSegment(_sColorRGBA8* src_img, int src_w, int src_h, int x, int y, int w, int h) {
    int i, j;
    _sColorRGBA8 c = { 0 };
    int limit_x = x + w;
    int limit_y = y + h;
    _sColorRGBA8* data = malloc(sizeof(_sColorRGBA8) * w * h);
    assert(src_img != 0);
    if (!data) { return 0; }
    // this also sets alpha to 0 so when mapped to palette anything missed by this function gets mapped to color 0 (transparent)
    memset(data, 0, sizeof(_sColorRGBA8) * w * h);

    if (limit_x > src_w) limit_x = src_w;
    if (limit_y > src_h) limit_y = src_h;

    for (j = y; j < limit_y; j++) {
        for (i = x; i < limit_x; i++) {
            c = src_img[i + (j * src_w)]; // i had skill issue, it was i, j not x, y
            data[(i - x) + ((j - y) * w)] = c;
        }
    }
    return data;
}


void BuildPaletteFromImage(_sColorRGBA8* p_img, int img_w, int img_h) {
    int i, j, w = img_w, h = img_h;
    _sColorRGBA8 c = { 0 };
    if (w > 16) w = 16;
    if (h > 16) h = 16;

    palette_size_x = w;
    palette_size_y = h;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            c = p_img[(j * img_w) + i];
            palette[j][i] = c;
        }
    }
}

void AutoBuildPalette(_sColorRGBA8* src, int w, int h) {
    int i, j;
    _sColorRGBA8 c = { 0 };
    _tColorIndexed index = 0;
    int colors_picked = 0;
    int aux1, aux2;
    palette_size_x = 16;
    palette_size_y = 0;

    // scan entire image
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            // read a single pixel
            c = src[i + (j * w)];

            if (c.a == 0) {
                // skip transparent pixels
                if (colors_picked++ == 0) {
                    palette[0][0] = c;
                }
                continue;
            }
            if (IsColorInPalette(c, 0, 8, &index)) {
                // color already in palette
                continue;
            }
            // new color
            aux1 = colors_picked++;
            aux2 = 0;
            while (aux1 > 15) { aux1 -= 16; aux2++; }
            palette[aux2][aux1] = c;
            if (colors_picked > 255) goto done;
        }
    }
done:
    palette_size_y = (colors_picked + 15) / 16;
}

uint8_t IsColorInPalette(_sColorRGBA8 color, uint8_t palette_y, _eBpp palette_bpp, _tColorIndexed* index) {
    int i, j, iter, j_low, j_high;
    _sColorRGBA8 c;
    switch (palette_bpp) {
    case 1:
        iter = 2;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    case 2:
        iter = 4;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    case 4:
        iter = 16;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    default:
        iter = 16;
        j_low = 0;
        j_high = 16;
        break;
    }
    for (j = j_low; j < j_high; j++) {
        for (i = 0; i < iter; i++) {
            c = palette[j][i];
            if ((color.r == c.r) && (color.g == c.g) && (color.b == c.b)) {
                // found
                if (palette_bpp == 8) {
                    *index = (j * 16) + i;
                } else {
                    *index = i;
                }
                return 1;
            }
        }
    }
    return 0;
}

void GetClosestColorInPalette(_sColorRGBA8 color, uint8_t palette_y, _eBpp palette_bpp, _tColorIndexed* index, float* dist) {
    int i, j, iter, j_low, j_high;
    float best_score = 1;
    float score = 1;
    _tColorIndexed best_index = 0;
    _sColorRGBA8 c;

    // transparency is always 0 so we can exit early
    if (color.a == 0) {
        *index = 0;
        *dist = 0;
        return;
    }

    switch (palette_bpp) {
    case 1:
        iter = 2;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    case 2:
        iter = 4;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    case 4:
        iter = 16;
        j_low = palette_y;
        j_high = palette_y + 1;
        break;
    default:
        iter = 16;
        j_low = 0;
        j_high = 16;
        break;
    }

    for (j = j_low; j < j_high; j++) {
        for (i = 0; i < iter; i++) {
            c = palette[j][i];
            score = GetColorDistance(color, c);
            if (score < best_score) {
                best_score = score;
                best_index = i;
                if (palette_bpp == 8) { best_index += (j * 16); }
            }
        }

    }

    *index = best_index;
    *dist = best_score;
}

float GetColorDistance(_sColorRGBA8 c1, _sColorRGBA8 c2) {
    if (c1.a == 0 && c2.a == 0) {
        return 0;
    }
    if (c1.a == 0 || c2.a == 0) {
        // only one is transparent, whatever the highest difference is
        return 1;
    }
    // neither is transparent
    float rdist = (c1.r - c2.r);
    float gdist = (c1.g - c2.g);
    float bdist = (c1.b - c2.b);
    rdist /= 256;
    gdist /= 256;
    bdist /= 256;
    // distance is actually the square root of this but we are only comparing so it's "good enough"
    return (rdist * rdist) + (gdist * gdist) + (bdist * bdist);
}

void PrintPaletteRGBA8(uint8_t palette_y) {
    uint8_t i = palette_y;
    printf("(ABGR) palette %i:\n", i);
    printf("%x %x %x %x ", *(uint32_t*)&palette[i][0], *(uint32_t*)&palette[i][1], *(uint32_t*)&palette[i][2], *(uint32_t*)&palette[i][3]);
    printf("%x %x %x %x\n", *(uint32_t*)&palette[i][4], *(uint32_t*)&palette[i][5], *(uint32_t*)&palette[i][6], *(uint32_t*)&palette[i][7]);
    printf("%x %x %x %x ", *(uint32_t*)&palette[i][8], *(uint32_t*)&palette[i][9], *(uint32_t*)&palette[i][10], *(uint32_t*)&palette[i][11]);
    printf("%x %x %x %x\n", *(uint32_t*)&palette[i][12], *(uint32_t*)&palette[i][13], *(uint32_t*)&palette[i][14], *(uint32_t*)&palette[i][15]);
}

void PrintHelp(int argn) {
    switch (argn) {
    case 1:
        printf("source_file: name (path to) input file\n accepted formats: .bmp .png");
        break;
    case 2:
        printf("tile_width: width encoded in output file, valid values:\n");
        printf("(selection of tile or bitmap mode is implied from value of tile_width)\n");
        printf("Tile/Sprite graphics: 8 16 32 64\n");
        printf("Bitmap graphics: 320 640\n");
        printf("If source image is wider than tile_width:\n");
        printf("Tiles/Sprites will be split into multiple tiles (you can convert an entire spritesheet at once)\n");
        printf("Bitmaps will be cropped");
        break;
    case 3:
        printf("tile_height: height encoded in output file, valid values:\n");
        printf("Tile/Sprite graphics: 8 16 32 64\n");
        printf("Bitmap graphics: any (value becomes max height of output)\n");
        printf("If source image is taller than tile_height:\n");
        printf("Tiles/Sprites will be split into multiple tiles (you can convert an entire spritesheet at once)\n");
        printf("Bitmaps will be cropped");
        break;
    case 4:
        printf("color_depth: bits per pixel (bpp) encoded in output file, ei number of colors in palette, valid values:\n");
        printf("1 (monochrome)\n2 (4 colors)\n4 (16 colors)\n8 (256 colors)\n");
        printf("note that for all of these color 0 is rendered as transparent\n");
        break;
    case 5:
        printf("palette_file: path to another image file used to pick colors from\n");
        printf("this should be a 16x16 or 16x1 image\n");
        printf("if palette_file param isn't provided the first 2/4/16/256 colors of source image will be picked as a palette\n");
        printf("each pixel in source will be approximated to closest color in palette");
        printf("for 1/2/4 bpp tiles/sprites, for each tile it will *try* to pick the closest palette (assuming you provided a 16x16 one)");
        break;

    default:
        printf("Usage:\n.\\Img2VeraBin.exe [source_file] [tile_width] [tile_height] [color_depth] (optional)[palette_file]\n");
        printf("Example:\n.\\Img2VeraBin.exe my_sprite_sheet.png 32 32 4 my_palette.png\n");
        printf("You can also enter:\n.\\Img2VeraBin.exe help [param_number]\n for more details on each parameter\n");
        break;
    }
}