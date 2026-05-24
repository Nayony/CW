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
float maxF(float a, float b, float c)
{
    float mx = 123;
    if (a > b)
    {
        mx = a;
    }
    else
    {
        mx = b;
    }
    if (mx > c)
    {
        return mx;
    }
    else
    {
        return c;
    }
    return mx;
}
float minF(float a, float b, float c)
{
    float mx = 0;
    if (a > b)
    {
        mx = b;
    }
    else
    {
        mx = a;
    }
    if (mx > c)
    {
        return c;
    }
    else
    {
        return mx;
    }
    return mx;
}

void hsv_img(Rgb **arr, int Height, int W)
{
    float new_R;
    float new_G;
    float new_B;
    float x_max;
    float x_min;
    float C;
    float H;
    float S, V;

    int h, s, v;
    for (int y = 0; y < Height; y++)
    {
        for (int x = 0; x < W; x++)
        {
            new_R = (arr[Height - y - 1][x].r / 255.0);
            new_G = (arr[Height - y - 1][x].g / 255.0);
            new_B = (arr[Height - y - 1][x].b / 255.0);
            x_max = maxF(new_R, new_G, new_B);
            x_min = minF(new_R, new_G, new_B);
            // printf("%f.%f\n", x_max, x_min);
            C = x_max - x_min;

            if (x_max == new_R && new_G >= new_B)
            {
                H = 60 * (((new_G - new_B) / C));
            }
            else if (x_max == new_R && new_G < new_B)
            {
                H = 60 * (((new_G - new_B) / C)) + 360;
            }
            else if (x_max == new_G)
            {
                H = 60 * (((new_B - new_R) / C)) + 120;
            }
            else if (x_max == new_B)
            {
                H = 60 * (((new_R - new_G) / C)) + 240;
            }
            if (x_max == 0)
            {
                S = 0;
            }
            else
            {
                S = C / x_max;
            }
            if (H < 0)
                H = H + 360;
            V = x_max;
            H = H / 2;
            S = S * 255;
            V = V * 255;
            h = floor(H);
            s = floor(S);
            v = floor(V);

            // printf("%d.%d.%d\n", h, s, v);
            arr[Height - y - 1][x].b = h;
            arr[Height - y - 1][x].g = s;
            arr[Height - y - 1][x].r = v;
        }
    }
}
void diamond(Rgb **arr, int H, int W, int x, int y, int size, int *fill_color)
{
    int r = floor(size * sqrt(2) / 2);
    for (int py = 0; py < H; py++)
    {
        for (int px = 0; px < W; px++)
        {
            if (abs(px - x) + abs(py - (y + r)) <= r)
            {

                arr[H - py - 1][px].r = fill_color[0];
                arr[H - py - 1][px].g = fill_color[1];
                arr[H - py - 1][px].b = fill_color[2];
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
    int do_info;
    int do_hsv;
    int do_square_rhombus;
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
    int x, y, size;
} AppState;

static void parse_cli(int argc, char *argv[], AppState *st)
{
    static struct option long_opts[] = {{"line", no_argument, 0, 'l'},
                                        {"inverse_circle", no_argument, 0, 'v'},
                                        {"trim", no_argument, 0, 'm'},
                                        {"hsv", no_argument, 0, 'H'},
                                        {"square_rhombus", no_argument, 0, 'S'},
                                        {"start", required_argument, 0, 's'},
                                        {"end", required_argument, 0, 'e'},
                                        {"color", required_argument, 0, 'c'},
                                        {"thickness", required_argument, 0, 't'},
                                        {"center", required_argument, 0, 'n'},
                                        {"radius", required_argument, 0, 'r'},
                                        {"left_up", required_argument, 0, 'u'},
                                        {"right_down", required_argument, 0, 'd'},
                                        {"upper_vertex", required_argument, 0, 'U'},
                                        {"size", required_argument, 0, 'Z'},
                                        {"fill_color", required_argument, 0, 'F'},
                                        {"info", no_argument, 0, 'i'},
                                        {"input", required_argument, 0, 'I'},
                                        {"output", required_argument, 0, 'o'},
                                        {"help", no_argument, 0, 'h'},
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
        case 'S':
            st->do_square_rhombus = 1;
            st->act_count++;
            break;
        case 'H':
            st->do_hsv = 1;
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
        case 'U':
            if (sscanf(optarg, "%d.%d", &st->x, &st->y) != 2)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --upper_vertex");
            break;
        case 'Z':
            if (sscanf(optarg, "%d", &st->size) != 1)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --size");
            break;
        case 'F':
            if (sscanf(optarg, "%d.%d.%d", &st->r, &st->g, &st->b) != 3)
                exit_err(ERR_ARGS, "Ошибка: неверный формат --rgb");
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
    else if (st.do_hsv)
    {
        hsv_img(arr, H, W);
    }
    else if (st.do_square_rhombus)
    {
        int color[3] = {st.r, st.g, st.b};
        diamond(arr, H, W, st.x, st.y, st.size, color);
    }
    write_bmp(st.output, arr, H, W, bmfh, bmif);
    free_arr(arr, H);
    return 0;
}