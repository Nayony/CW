#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERR_GENERAL 40
#define ERR_COLOR 41
#define ERR_ARGS 42
#define ERR_FILE 43
#define ERR_ACTIONS 44

#pragma pack(push, 1)

typedef struct
{
    unsigned short signature;
    unsigned int filesize;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int pixelArrOffset;
} BitmapFileHeader;

typedef struct
{
    unsigned int headerSize;
    unsigned int width;
    unsigned int height;
    unsigned short planes;
    unsigned short bitsPerPixel;
    unsigned int compression;
    unsigned int imageSize;
    unsigned int xPixelsPerMeter;
    unsigned int yPixelsPerMeter;
    unsigned int colorsInColorTable;
    unsigned int importantColorCount;
} BitmapInfoHeader;

typedef struct
{
    unsigned char b;
    unsigned char g;
    unsigned char r;
} Rgb;

#pragma pack(pop)

static void exit_err(int code, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(code);
}

static int row_size(int W)
{
    return W * 3 + (4 - (W * 3) % 4) % 4;
}

Rgb **read_bmp(char file_name[], BitmapFileHeader *bmfh, BitmapInfoHeader *bmif)
{
    FILE *f = fopen(file_name, "rb");
    if (!f)
        exit_err(ERR_FILE, "Ошибка: не удалось открыть '%s'", file_name);

    if (fread(bmfh, 1, sizeof(BitmapFileHeader), f) != sizeof(BitmapFileHeader) ||
        fread(bmif, 1, sizeof(BitmapInfoHeader), f) != sizeof(BitmapInfoHeader))
    {
        fclose(f);
        exit_err(ERR_FILE, "Ошибка: повреждён заголовок BMP");
    }

    if (bmfh->signature != 0x4D42)
    {
        fclose(f);
        exit_err(ERR_FILE, "Ошибка: не BMP файл");
    }
    if (bmif->bitsPerPixel != 24)
    {
        fclose(f);
        exit_err(ERR_FILE, "Ошибка: только 24-bit BMP");
    }
    if (bmif->compression != 0)
    {
        fclose(f);
        exit_err(ERR_FILE, "Ошибка: сжатие не поддерживается");
    }

    fseek(f, bmfh->pixelArrOffset, SEEK_SET);

    unsigned int H = bmif->height;
    unsigned int W = bmif->width;
    int rs = row_size(W);

    Rgb **arr = malloc(H * sizeof(Rgb *));
    if (!arr)
    {
        fclose(f);
        exit_err(ERR_FILE, "Ошибка: нет памяти");
    }

    for (unsigned int i = 0; i < H; i++)
    {
        arr[i] = malloc(rs);
        if (!arr[i])
        {
            for (unsigned int k = 0; k < i; k++)
                free(arr[k]);
            free(arr);
            fclose(f);
            exit_err(ERR_FILE, "Ошибка: нет памяти");
        }
        if ((int)fread(arr[i], 1, rs, f) != rs)
        {
            for (unsigned int k = 0; k <= i; k++)
                free(arr[k]);
            free(arr);
            fclose(f);
            exit_err(ERR_FILE, "Ошибка: конец файла");
        }
    }

    fclose(f);
    return arr;
}

void write_bmp(const char file_name[], Rgb **arr, int H, int W, BitmapFileHeader bmfh, BitmapInfoHeader bmif)
{
    FILE *ff = fopen(file_name, "wb");
    if (!ff)
        exit_err(ERR_FILE, "Ошибка: не удалось создать файл '%s'", file_name);

    fwrite(&bmfh, 1, sizeof(BitmapFileHeader), ff);
    fwrite(&bmif, 1, sizeof(BitmapInfoHeader), ff);

    int rs = row_size(W);
    for (int i = 0; i < H; i++)
        fwrite(arr[i], 1, rs, ff);

    fclose(ff);
}

void free_arr(Rgb **arr, int H)
{
    for (int i = 0; i < H; i++)
        free(arr[i]);
    free(arr);
}

void draw_thick_pixel(Rgb **arr, int H, int W, int x, int y, int *color, int thickness)
{
    if (thickness <= 0)
        return;
    int radius = (thickness - 1) / 2;
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            int px = x + dx;
            int py = H - y - 1 + dy;
            if (px >= 0 && px < W && py >= 0 && py < H)
            {
                arr[py][px].r = color[0];
                arr[py][px].g = color[1];
                arr[py][px].b = color[2];
            }
        }
    }
}

void draw_line(Rgb **arr, int H, int W, int x0, int y0, int x1, int y1, int thickness, int *color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int e2;

    while (x0 != x1 || y0 != y1)
    {
        draw_thick_pixel(arr, H, W, x0, y0, color, thickness);
        e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
    draw_thick_pixel(arr, H, W, x1, y1, color, thickness);
}

void inverse_color(Rgb *px)
{
    px->r = 255 - px->r;
    px->g = 255 - px->g;
    px->b = 255 - px->b;
}

int distance_sq(int x0, int y0, int x1, int y1)
{
    return (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
}

void inverse_circle(Rgb **arr, int H, int W, int x_c, int y_c, int radius)
{
    int r2 = radius * radius;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            if (distance_sq(x, y, x_c, y_c) <= r2)
                inverse_color(&arr[H - y - 1][x]);
}

Rgb **do_trim(Rgb **arr, int H, int W, int x0, int y0, int x1, int y1)
{
    if (x0 >= x1 || y0 >= y1 || x0 < 0 || y0 < 0 || x1 > W || y1 > H)
        exit_err(ERR_GENERAL, "Ошибка: некорректные координаты обрезки");

    int new_H = y1 - y0;
    int new_W = x1 - x0;
    int rs = row_size(new_W);

    Rgb **res = malloc(new_H * sizeof(Rgb *));
    if (!res)
        exit_err(ERR_FILE, "Ошибка: нет памяти");

    for (int y = y0; y < y1; y++)
    {
        int row = new_H - 1 - (y - y0);
        res[row] = malloc(rs);
        if (!res[row])
        {
            for (int k = 0; k < new_H; k++)
                if (res[k])
                    free(res[k]);
            free(res);
            exit_err(ERR_FILE, "Ошибка: нет памяти");
        }
        for (int x = x0; x < x1; x++)
            res[row][x - x0] = arr[H - y - 1][x];
    }
    return res;
}
void binarization(Rgb **arr, int H, int W, int threshold)
{
    if (threshold < 0 || threshold > 765)
    {
        exit_err(ERR_ARGS, "Ошибка: неверный формат --Неверное значение threshold");
    }
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            if ((arr[H - y - 1][x].r + arr[H - y - 1][x].g + arr[H - y - 1][x].b) >= threshold)
            {
                arr[H - y - 1][x].r = 255;
                arr[H - y - 1][x].g = 255;
                arr[H - y - 1][x].b = 255;
            }
            else
            {
                arr[H - y - 1][x].r = 0;
                arr[H - y - 1][x].g = 0;
                arr[H - y - 1][x].b = 0;
            }
        }
    }
}

void print_info(BitmapFileHeader *bmfh, BitmapInfoHeader *bmif)
{
    printf("BMP File Information:\n");
    printf("  Header signature: 0x%04X\n", bmfh->signature);
    printf("  File Size: %u bytes\n", bmfh->filesize);
    printf("  Pixel Offset: %u\n", bmfh->pixelArrOffset);
    printf("  Info Header Size: %u\n", bmif->headerSize);
    printf("  Width: %d pixels\n", bmif->width);
    printf("  Height: %d pixels\n", bmif->height);
    printf("  Planes: %u\n", bmif->planes);
    printf("  Bits Per Pixel: %u\n", bmif->bitsPerPixel);
    printf("  Compression: %u\n", bmif->compression);
    printf("  Image Size: %u bytes\n", bmif->imageSize);
    printf("  X Pixels Per Meter: %u\n", bmif->xPixelsPerMeter);
    printf("  Y Pixels Per Meter: %u\n", bmif->yPixelsPerMeter);
    printf("  Colors Used: %u\n", bmif->colorsInColorTable);
    printf("  Important Colors: %u\n", bmif->importantColorCount);
}

void help()
{
    puts("Usage: ./cw [options] [input_file]");
    puts("Options:");
    puts("  -h, --help                  Show this help message and exit.");
    puts("  -i, --info                  Show information about the input BMP file.");
    puts("  -o, --output FILE           Output BMP file name (default: out.bmp).");
    puts("");
    puts("Operations (select one):");
    puts("  --line                      Draw a line.");
    puts("    --start X.Y               Coordinates of the start of the line");
    puts("    --end X.Y                 Coordinates of the end of the line");
    puts("    --thickness N             Thickness of the line. Must be >= 1.");
    puts("    --color R.G.B             Line color (RGB values 0-255).");
    puts("");
    puts("  --inverse_circle            Invert the color in a circle on an image.");
    puts("    --center X.Y              Center coordinates for circle.");
    puts("    --radius N                Radius of the circle. Must be >= 1.");
    puts("");
    puts("  --trim                      Crops the input image to the specified size.");
    puts("    --left_up X.Y             Coordinates of the upper-left corner of the image.");
    puts("    --right_down X.Y          Coordinates of the down-right corner of the image.");
}

typedef struct
{
    int do_line;
    int do_inverse;
    int do_trim;
    int do_bin;
    int threshold;
    int do_info;
    int act_count;
    char *input;
    char *output;
    int start_x, start_y;
    int end_x, end_y;
    int r, g, b;
    int thickness;
    int x_c, y_c;
    int radius;
    int left, up, right, down;
} AppState;

static void parse_cli(int argc, char *argv[], AppState *st)
{
    static struct option long_opts[] = {{"line", no_argument, 0, 'l'},
                                        {"inverse_circle", no_argument, 0, 'v'},
                                        {"trim", no_argument, 0, 'm'},
                                        {"start", required_argument, 0, 's'},
                                        {"end", required_argument, 0, 'e'},
                                        {"color", required_argument, 0, 'c'},
                                        {"thickness", required_argument, 0, 't'},
                                        {"center", required_argument, 0, 'n'},
                                        {"radius", required_argument, 0, 'r'},
                                        {"left_up", required_argument, 0, 'u'},
                                        {"right_down", required_argument, 0, 'd'},
                                        {"info", no_argument, 0, 'i'},
                                        {"input", required_argument, 0, 'I'},
                                        {"output", required_argument, 0, 'o'},
                                        {"help", no_argument, 0, 'h'},
                                        {"binarization", no_argument, 0, 'B'},
                                        {"threshold", required_argument, 0, 'T'},
                                        {0, 0, 0, 0}};

    st->thickness = 1;
    st->output = "out.bmp";

    int opt, idx = 0;
    opterr = 0;

    while ((opt = getopt_long(argc, argv, "lvm:s:e:c:t:n:r:u:d:io:h", long_opts, &idx)) != -1)
    {
        switch (opt)
        {
        case 'h':
            help();
            exit(0);
        case 'B':
            st->do_bin = 1;
            st->act_count++;
            break;
        case 'T':
            if (sscanf(optarg, "%d", &st->threshold) != 1)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --thresh");
            break;
        case 'i':
            st->do_info = 1;
            st->act_count++;
            break;
        case 'I':
            st->input = optarg;
            break;
        case 'o':
            st->output = optarg;
            break;
        case 'l':
            st->do_line = 1;
            st->act_count++;
            break;
        case 'v':
            st->do_inverse = 1;
            st->act_count++;
            break;
        case 'm':
            st->do_trim = 1;
            st->act_count++;
            break;
        case 's':
            if (sscanf(optarg, "%d.%d", &st->start_x, &st->start_y) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --start");
            break;
        case 'e':
            if (sscanf(optarg, "%d.%d", &st->end_x, &st->end_y) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --end");
            break;
        case 'c':
            if (sscanf(optarg, "%d.%d.%d", &st->r, &st->g, &st->b) != 3)
                exit_err(ERR_COLOR, "Ошибка: неверный формат --color");
            if (st->r < 0 || st->r > 255 || st->g < 0 || st->g > 255 || st->b < 0 || st->b > 255)
                exit_err(ERR_COLOR, "Ошибка: значения цвета должны быть 0-255");
            break;
        case 't':
            if (sscanf(optarg, "%d", &st->thickness) != 1 || st->thickness < 1)
                exit_err(ERR_ARGS, "Ошибка: --thickness должен быть >= 1");
            break;
        case 'n':
            if (sscanf(optarg, "%d.%d", &st->x_c, &st->y_c) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --center");
            break;
        case 'r':
            if (sscanf(optarg, "%d", &st->radius) != 1 || st->radius < 1)
                exit_err(ERR_ARGS, "Ошибка: --radius должен быть >= 1");
            break;
        case 'u':
            if (sscanf(optarg, "%d.%d", &st->left, &st->up) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --left_up");
            break;
        case 'd':
            if (sscanf(optarg, "%d.%d", &st->right, &st->down) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --right_down");
            break;
        case '?':
            exit_err(ERR_ARGS, "Ошибка: неизвестный флаг");
        }
    }

    if (!st->input)
    {
        if (optind < argc)
            st->input = argv[optind];
        else
            exit_err(ERR_FILE, "Ошибка: не указан входной файл");
    }

    if (st->act_count == 0 && !st->do_info)
        exit_err(ERR_ARGS, "Ошибка: действие не выбрано");

    if (st->act_count > 1)
        exit_err(ERR_ACTIONS, "Ошибка: допустимо только 1 действие");
}

int main(int argc, char *argv[])
{
    puts("Course work for option 4.6, created by Fadeev Aleksandr.\n");

    if (argc == 1)
    {
        help();
        return 0;
    }

    AppState st = {0};
    parse_cli(argc, argv, &st);

    BitmapFileHeader bmfh;
    BitmapInfoHeader bmif;
    Rgb **arr = read_bmp(st.input, &bmfh, &bmif);

    int W = bmif.width;
    int H = bmif.height;

    if (st.do_info)
    {
        print_info(&bmfh, &bmif);
        free_arr(arr, H);
        return 0;
    }

    if (st.do_line)
    {
        int color[3] = {st.r, st.g, st.b};
        draw_line(arr, H, W, st.start_x, st.start_y, st.end_x, st.end_y, st.thickness, color);
    }
    else if (st.do_inverse)
    {
        inverse_circle(arr, H, W, st.x_c, st.y_c, st.radius);
    }
    else if (st.do_bin)
    {
        binarization(arr, H, W, st.threshold);
    }
    else if (st.do_trim)
    {
        if (st.left > st.right)
        {
            int tmp = st.left;
            st.left = st.right;
            st.right = tmp;
        }
        if (st.up > st.down)
        {
            int tmp = st.up;
            st.up = st.down;
            st.down = tmp;
        }

        Rgb **new_arr = do_trim(arr, H, W, st.left, st.up, st.right, st.down);
        free_arr(arr, H);
        arr = new_arr;
        H = st.down - st.up;
        W = st.right - st.left;
        bmif.width = W;
        bmif.height = H;
        int rs = row_size(W);
        bmif.imageSize = rs * H;
        bmfh.filesize = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + bmif.imageSize;
    }

    write_bmp(st.output, arr, H, W, bmfh, bmif);
    free_arr(arr, H);
    return 0;
}