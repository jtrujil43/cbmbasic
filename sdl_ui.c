#include "sdl_ui.h"

#include <stdio.h>

#ifdef HAVE_SDL2
#include <math.h>
#include <stdlib.h>
#include <SDL.h>
#include "glue.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static Uint32 *pixels;
static SDL_Color colors[256];
static int screen_width;
static int screen_height;
static int pending_key;
static int quit_requested;
static int frame_open;
static int dirty;
static int dirty_left;
static int dirty_top;
static int dirty_right;
static int dirty_bottom;
static unsigned short stroke_width = 1;
static unsigned short font_scale = 1;
static unsigned int pixel_batch;
static Uint32 last_present;
static int vic2_screen;
static Uint32 vic2_hash;
static const unsigned char *glyph(unsigned char ch);
static const SDL_Color default_palette[16] = {
	{0, 0, 0, 255}, {255, 255, 255, 255},
	{136, 0, 0, 255}, {170, 255, 238, 255},
	{204, 68, 204, 255}, {0, 204, 85, 255},
	{0, 0, 170, 255}, {238, 238, 119, 255},
	{221, 136, 85, 255}, {102, 68, 0, 255},
	{255, 119, 119, 255}, {51, 51, 51, 255},
	{119, 119, 119, 255}, {170, 255, 102, 255},
	{0, 136, 255, 255}, {187, 187, 187, 255}
};

static void
mark_dirty(int x, int y, int width, int height)
{
	int right = x + width;
	int bottom = y + height;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (right > screen_width)
		right = screen_width;
	if (bottom > screen_height)
		bottom = screen_height;
	if (x >= right || y >= bottom)
		return;

	if (!dirty) {
		dirty_left = x;
		dirty_top = y;
		dirty_right = right;
		dirty_bottom = bottom;
		dirty = 1;
	} else {
		if (x < dirty_left)
			dirty_left = x;
		if (y < dirty_top)
			dirty_top = y;
		if (right > dirty_right)
			dirty_right = right;
		if (bottom > dirty_bottom)
			dirty_bottom = bottom;
	}
}

static Uint32
color_pixel(unsigned short color, int x, int y)
{
	Uint32 old, red, green, blue;
	unsigned int alpha = colors[color].a;
	size_t index = (size_t)y * screen_width + x;

	if (alpha == 255)
		return 0xFF000000u | ((Uint32)colors[color].r << 16) |
		    ((Uint32)colors[color].g << 8) | colors[color].b;
	if (alpha == 0)
		return pixels[index];
	old = pixels[index];
	red = (colors[color].r * alpha + ((old >> 16) & 255) *
	    (255 - alpha) + 127) / 255;
	green = (colors[color].g * alpha + ((old >> 8) & 255) *
	    (255 - alpha) + 127) / 255;
	blue = (colors[color].b * alpha + (old & 255) *
	    (255 - alpha) + 127) / 255;
	return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static void
poll_events(void)
{
	SDL_Event event;
	int key_received = 0;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			quit_requested = 1;
			pending_key = 27;
			key_received = 1;
		} else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
			SDL_Keycode key = event.key.keysym.sym;
			if (key >= 32 && key <= 126)
				pending_key = (int)key;
			else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
				pending_key = 13;
			else if (key == SDLK_ESCAPE)
				pending_key = 27;
			else
				pending_key = 1;
			key_received = 1;
		} else if (event.type == SDL_MOUSEBUTTONDOWN) {
			pending_key = 1;
			key_received = 1;
		}
	}

	if (key_received) {
		RAM[SDL_RAM_KEY] = (unsigned char)pending_key;
		if (RAM[SDL_RAM_MODE] != 0)
			RAM[1024] = (unsigned char)pending_key;
	}
}

static void
present(void)
{
	SDL_Rect update;
	const Uint32 *start;

	if (renderer == NULL || texture == NULL || pixels == NULL)
		return;
	if (dirty) {
		update.x = dirty_left;
		update.y = dirty_top;
		update.w = dirty_right - dirty_left;
		update.h = dirty_bottom - dirty_top;
		start = pixels + (size_t)update.y * screen_width + update.x;
		if (SDL_UpdateTexture(texture, &update, start,
		    screen_width * (int)sizeof(*pixels)) != 0)
			fprintf(stderr, "graphics update: %s\n", SDL_GetError());
		dirty = 0;
	}
	pixel_batch = 0;
	last_present = SDL_GetTicks();
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	poll_events();
}

static void
close_screen(void)
{
	int extension_mode = RAM[SDL_RAM_MODE] != 0;

	if (dirty)
		present();
	if (texture != NULL)
		SDL_DestroyTexture(texture);
	if (renderer != NULL)
		SDL_DestroyRenderer(renderer);
	if (window != NULL)
		SDL_DestroyWindow(window);
	free(pixels);
	texture = NULL;
	renderer = NULL;
	window = NULL;
	pixels = NULL;
	screen_width = 0;
	screen_height = 0;
	frame_open = 0;
	dirty = 0;
	if (extension_mode)
		RAM[1042] = 0;
	RAM[SDL_RAM_MODE] = 0;
	vic2_screen = 0;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static int
open_screen(int width, int height, const char *title, int extension_mode)
{
	int window_width, window_height;
	int i;
	SDL_RendererFlags flags;

	close_screen();
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SCREEN: %s\n", SDL_GetError());
		return -1;
	}
	if (extension_mode) {
		int base_width = width / extension_mode;
		int base_height = height / extension_mode;

		RAM[SDL_RAM_WIDTH] = (unsigned char)(base_width & 255);
		RAM[SDL_RAM_WIDTH + 1] = (unsigned char)(base_width >> 8);
		RAM[SDL_RAM_HEIGHT] = (unsigned char)(base_height & 255);
		RAM[SDL_RAM_HEIGHT + 1] = (unsigned char)(base_height >> 8);
		RAM[SDL_RAM_MODE] = (unsigned char)extension_mode;
		RAM[1038] = (unsigned char)(base_width & 255);
		RAM[1039] = (unsigned char)(base_width >> 8);
		RAM[1040] = (unsigned char)(base_height & 255);
		RAM[1041] = (unsigned char)(base_height >> 8);
		RAM[1042] = (unsigned char)extension_mode;
	} else {
		RAM[SDL_RAM_MODE] = 0;
	}
	screen_width = width;
	screen_height = height;
	window_width = width < 960 ? width : 960;
	window_height = height < 720 ? height : 720;

	window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
	    SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_SHOWN);
	if (window == NULL) {
		fprintf(stderr, "SCREEN: %s\n", SDL_GetError());
		close_screen();
		return -1;
	}
	flags = SDL_RENDERER_ACCELERATED;
	renderer = SDL_CreateRenderer(window, -1, flags);
	if (renderer == NULL)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (renderer == NULL) {
		fprintf(stderr, "SCREEN: %s\n", SDL_GetError());
		close_screen();
		return -1;
	}
	SDL_RenderSetLogicalSize(renderer, width, height);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
	    SDL_TEXTUREACCESS_STREAMING, width, height);
	if (texture == NULL) {
		fprintf(stderr, "SCREEN: %s\n", SDL_GetError());
		close_screen();
		return -1;
	}
	if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) != 0) {
		fprintf(stderr, "SCREEN: %s\n", SDL_GetError());
		close_screen();
		return -1;
	}
	pixels = (Uint32 *)calloc((size_t)width * height, sizeof(*pixels));
	if (pixels == NULL) {
		fprintf(stderr, "SCREEN: out of memory\n");
		close_screen();
		return -1;
	}
	pending_key = 0;
	quit_requested = 0;
	for (i = 0; i < width * height; i++)
		pixels[i] = 0xFF000000u;
	mark_dirty(0, 0, width, height);
	stroke_width = 1;
	font_scale = 1;
	pixel_batch = 0;
	last_present = SDL_GetTicks();
	for (i = 0; i < 256; i++) {
		if (i < sizeof(default_palette) / sizeof(default_palette[0]))
			colors[i] = default_palette[i];
		else {
			colors[i].r = 0;
			colors[i].g = 0;
			colors[i].b = 0;
			colors[i].a = 255;
		}
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	present();
	return 0;
}

int
sdl_beep(unsigned short frequency, unsigned short duration)
{
	SDL_AudioSpec spec;
	SDL_AudioDeviceID device;
	Sint16 *samples;
	size_t count;
	size_t i;
	const double pi = 3.14159265358979323846;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "BEEP: %s\n", SDL_GetError());
		return -1;
	}

	SDL_zero(spec);
	spec.freq = 44100;
	spec.format = AUDIO_S16SYS;
	spec.channels = 1;
	device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
	if (device == 0) {
		fprintf(stderr, "BEEP: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return -1;
	}

	count = (size_t)spec.freq * duration / 1000;
	samples = (Sint16 *)malloc(count * sizeof(*samples));
	if (samples == NULL) {
		fprintf(stderr, "BEEP: out of memory\n");
		SDL_CloseAudioDevice(device);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return -1;
	}

	for (i = 0; i < count; i++)
		samples[i] = (Sint16)(12000.0 * sin(2.0 * pi * frequency * i / spec.freq));

	if (SDL_QueueAudio(device, samples, (Uint32)(count * sizeof(*samples))) != 0) {
		fprintf(stderr, "BEEP: %s\n", SDL_GetError());
		free(samples);
		SDL_CloseAudioDevice(device);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return -1;
	}
	free(samples);

	SDL_PauseAudioDevice(device, 0);
	while (SDL_GetQueuedAudioSize(device) != 0)
		SDL_Delay(1);
	SDL_CloseAudioDevice(device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	return 0;
}

int
sdl_frame(const char *title, const char *message)
{
	int result;
	int initialize_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;

	if (initialize_video && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "FRAME: %s\n", SDL_GetError());
		return -1;
	}

	result = SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
	    title, message, NULL);
	if (result != 0)
		fprintf(stderr, "FRAME: %s\n", SDL_GetError());
	if (initialize_video)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	return result;
}

int
sdl_screen(unsigned short mode)
{
	if (mode == 0) {
		close_screen();
		return 0;
	}
	if (mode > 3)
		return -1;

	if (window != NULL && mode == RAM[SDL_RAM_MODE])
		return 0;
	return open_screen(800 * mode, 400 * mode, "cbmbasic graphics", mode);
}

int
sdl_title(const char *title)
{
	if (window == NULL)
		return -1;
	SDL_SetWindowTitle(window, title);
	return 0;
}

int
sdl_color(unsigned short index, unsigned short red, unsigned short green,
    unsigned short blue, unsigned short alpha)
{
	if (index > 255 || red > 255 || green > 255 ||
	    blue > 255 || alpha > 255)
		return -1;
	colors[index].r = (Uint8)red;
	colors[index].g = (Uint8)green;
	colors[index].b = (Uint8)blue;
	colors[index].a = (Uint8)alpha;
	return 0;
}

int
sdl_clear(void)
{
	if (pixels == NULL)
		return -1;
	{
		size_t pixel_count = (size_t)screen_width * screen_height;
		size_t pixel;
		for (pixel = 0; pixel < pixel_count; pixel++)
			pixels[pixel] = 0xFF000000u;
	}
	mark_dirty(0, 0, screen_width, screen_height);
	present();
	return 0;
}

void
sdl_begin_frame(void)
{
	frame_open = 1;
}

void
sdl_end_frame(void)
{
	frame_open = 0;
	if (dirty && SDL_GetTicks() - last_present >= 16)
		present();
}

int
sdl_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color)
{
	unsigned int left, top, right, bottom;
	unsigned int row, column;

	if (pixels == NULL || color > 255)
		return -1;
	left = x1 < x2 ? x1 : x2;
	top = y1 < y2 ? y1 : y2;
	right = x1 < x2 ? x2 : x1;
	bottom = y1 < y2 ? y2 : y1;
	if (left > (unsigned int)screen_width)
		left = screen_width;
	if (top > (unsigned int)screen_height)
		top = screen_height;
	if (right > (unsigned int)screen_width)
		right = (unsigned int)screen_width;
	if (bottom > (unsigned int)screen_height)
		bottom = (unsigned int)screen_height;
	for (row = top; row < bottom; row++)
		for (column = left; column < right; column++)
			pixels[(size_t)row * screen_width + column] =
			    color_pixel(color, column, row);
	mark_dirty(left, top, right - left, bottom - top);
	if (!frame_open && dirty && SDL_GetTicks() - last_present >= 16)
		present();
	return 0;
}

int
sdl_pixel(unsigned short x, unsigned short y, unsigned short color)
{
	if (pixels == NULL || x >= screen_width || y >= screen_height || color > 255)
		return -1;
	pixels[(size_t)y * screen_width + x] = color_pixel(color, x, y);
	mark_dirty(x, y, 1, 1);
	if (!frame_open && ++pixel_batch >= 256) {
		pixel_batch = 0;
		if (SDL_GetTicks() - last_present >= 16)
			present();
	}
	return 0;
}

static int
draw_pixel(int x, int y, unsigned short color)
{
	int left, top, right, bottom, px, py;
	if (pixels == NULL || color > 255)
		return -1;
	left = x - (int)(stroke_width - 1) / 2;
	top = y - (int)(stroke_width - 1) / 2;
	right = left + stroke_width;
	bottom = top + stroke_width;
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > screen_width) right = screen_width;
	if (bottom > screen_height) bottom = screen_height;
	for (py = top; py < bottom; py++)
		for (px = left; px < right; px++)
			pixels[(size_t)py * screen_width + px] =
			    color_pixel(color, px, py);
	mark_dirty(left, top, right - left, bottom - top);
	return 0;
}

static void
finish_draw(void)
{
	if (!frame_open && dirty && SDL_GetTicks() - last_present >= 16)
		present();
}

int
sdl_line(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color)
{
	int x = x1, y = y1;
	int dx = abs((int)x2 - (int)x1), sx = x1 < x2 ? 1 : -1;
	int dy = -abs((int)y2 - (int)y1), sy = y1 < y2 ? 1 : -1;
	int error = dx + dy, twice;

	if (pixels == NULL || color > 255)
		return -1;
	for (;;) {
		draw_pixel(x, y, color);
		if (x == x2 && y == y2)
			break;
		twice = 2 * error;
		if (twice >= dy) { error += dy; x += sx; }
		if (twice <= dx) { error += dx; y += sy; }
	}
	finish_draw();
	return 0;
}

int
sdl_frame_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color)
{
	int was_frame_open = frame_open;

	if (pixels == NULL || color > 255)
		return -1;
	frame_open = 1;
	sdl_line(x1, y1, x2, y1, color);
	sdl_line(x2, y1, x2, y2, color);
	sdl_line(x2, y2, x1, y2, color);
	sdl_line(x1, y2, x1, y1, color);
	frame_open = was_frame_open;
	finish_draw();
	return 0;
}

int
sdl_circle(unsigned short x, unsigned short y, unsigned short radius,
    unsigned short color, unsigned short fill)
{
	int row, extent, angle, steps;

	if (pixels == NULL || color > 255)
		return -1;
	if (fill) {
		for (row = -(int)radius; row <= (int)radius; row++) {
			extent = (int)sqrt((double)radius * radius - (double)row * row);
			{
				int column;
				for (column = -extent; column <= extent; column++)
					draw_pixel((int)x + column, (int)y + row, color);
			}
		}
	} else {
		steps = radius * 8;
		if (steps < 12) steps = 12;
		if (steps > 30000) steps = 30000;
		for (angle = 0; angle < steps; angle++) {
			double a = 6.283185307179586 * angle / steps;
			draw_pixel((int)x + (int)(radius * cos(a)),
			    (int)y + (int)(radius * sin(a)), color);
		}
	}
	finish_draw();
	return 0;
}

int
sdl_ellipse(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color, unsigned short fill)
{
	int left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
	int top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
	double rx = (right - left) / 2.0, ry = (bottom - top) / 2.0;
	double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
	int x, y, steps, i;

	if (pixels == NULL || color > 255)
		return -1;
	if (fill) {
		int from_y = top < 0 ? 0 : top;
		int to_y = bottom >= screen_height ? screen_height - 1 : bottom;
		int from_x = left < 0 ? 0 : left;
		int to_x = right >= screen_width ? screen_width - 1 : right;
		for (y = from_y; y <= to_y; y++)
			for (x = from_x; x <= to_x; x++) {
				double nx = rx == 0 ? 0 : (x - cx) / rx;
				double ny = ry == 0 ? 0 : (y - cy) / ry;
				if (nx * nx + ny * ny <= 1.0)
					draw_pixel(x, y, color);
			}
	} else {
		steps = (int)(8 * (rx + ry));
		if (steps < 12) steps = 12;
		if (steps > 30000) steps = 30000;
		for (i = 0; i < steps; i++) {
			double a = 6.283185307179586 * i / steps;
			draw_pixel((int)(cx + rx * cos(a)),
			    (int)(cy + ry * sin(a)), color);
		}
	}
	finish_draw();
	return 0;
}

int
sdl_arc(unsigned short x, unsigned short y, unsigned short radius,
    unsigned short start, unsigned short end, unsigned short color,
    unsigned short fill)
{
	double begin = start * 0.017453292519943295;
	double finish = end * 0.017453292519943295;
	int was_frame_open = frame_open;
	int steps, i, px = x, py = y;

	if (pixels == NULL || color > 255)
		return -1;
	if (finish < begin)
		finish += 6.283185307179586;
	steps = (int)(radius * (finish - begin) / 2.0);
	if (steps < 1) steps = 1;
	if (steps > 30000) steps = 30000;
	frame_open = 1;
	for (i = 0; i <= steps; i++) {
		double a = begin + (finish - begin) * i / steps;
		int nx = (int)x + (int)(radius * cos(a));
		int ny = (int)y + (int)(radius * sin(a));
		if (fill)
			sdl_line(x, y, (unsigned short)nx, (unsigned short)ny, color);
		else if (i != 0)
			sdl_line((unsigned short)px, (unsigned short)py,
			    (unsigned short)nx, (unsigned short)ny, color);
		px = nx;
		py = ny;
	}
	frame_open = was_frame_open;
	finish_draw();
	return 0;
}

int
sdl_triangle(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short x3, unsigned short y3,
    unsigned short color, unsigned short fill)
{
	int min_x, max_x, min_y, max_y, x, y;
	long area, a, b, c;

	if (pixels == NULL || color > 255)
		return -1;
	if (!fill) {
		sdl_line(x1, y1, x2, y2, color);
		sdl_line(x2, y2, x3, y3, color);
		sdl_line(x3, y3, x1, y1, color);
		return 0;
	}
	min_x = x1 < x2 ? x1 : x2;
	if (x3 < min_x) min_x = x3;
	max_x = x1 > x2 ? x1 : x2;
	if (x3 > max_x) max_x = x3;
	min_y = y1 < y2 ? y1 : y2;
	if (y3 < min_y) min_y = y3;
	max_y = y1 > y2 ? y1 : y2;
	if (y3 > max_y) max_y = y3;
	if (max_x >= screen_width) max_x = screen_width - 1;
	if (max_y >= screen_height) max_y = screen_height - 1;
	area = ((long)x2 - x1) * ((long)y3 - y1) -
	    ((long)y2 - y1) * ((long)x3 - x1);
	if (area == 0)
		return sdl_line(x1, y1, x2, y2, color);
	for (y = min_y; y <= max_y; y++)
		for (x = min_x; x <= max_x; x++) {
			a = ((long)x2 - x) * ((long)y3 - y) -
			    ((long)y2 - y) * ((long)x3 - x);
			b = ((long)x3 - x) * ((long)y1 - y) -
			    ((long)y3 - y) * ((long)x1 - x);
			c = ((long)x1 - x) * ((long)y2 - y) -
			    ((long)y1 - y) * ((long)x2 - x);
			if ((area > 0 && a >= 0 && b >= 0 && c >= 0) ||
			    (area < 0 && a <= 0 && b <= 0 && c <= 0))
				draw_pixel(x, y, color);
		}
	finish_draw();
	return 0;
}

static const unsigned char *
glyph(unsigned char ch)
{
	static const unsigned char digits[10][7] = {
		{14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
		{14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
		{2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
		{14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
		{14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
	};
	static const unsigned char letters[26][7] = {
		{14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
		{14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
		{31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
		{14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
		{14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
		{17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
		{17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
		{14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
		{14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
		{15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
		{17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
		{17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
		{17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
	};
	static const unsigned char blank[7] = {0,0,0,0,0,0,0};
	static const unsigned char punctuation[][7] = {
		{0,0,0,0,0,0,0}, {0,4,4,4,4,0,4}, {0,0,0,31,0,0,0},
		{0,0,0,0,0,4,4}, {0,1,2,4,8,16,0}, {0,0,0,0,0,0,4},
		{0,4,4,4,4,4,4}, {0,0,4,0,0,4,0}, {0,10,10,0,0,0,0},
		{0,10,31,10,31,10,0}, {0,4,15,20,14,5,30}
	};
	static const unsigned char extra[][7] = {
		{4,4,2,0,0,0,0}, {10,31,10,10,31,10,0},
		{17,2,4,8,17,0,0}, {12,18,20,8,21,18,13},
		{2,4,8,8,8,4,2}, {8,4,2,2,2,4,8},
		{0,0,21,14,31,14,21}, {0,4,4,31,4,4,0},
		{0,0,0,31,0,0,0}, {2,4,8,16,8,4,2},
		{8,4,2,1,2,4,8}, {14,8,8,8,8,8,14},
		{14,2,2,2,2,2,14}, {0,0,0,0,0,0,31},
		{4,10,17,0,0,0,0}, {14,17,23,21,23,16,14},
		{16,8,4,2,1,0,0}, {8,4,2,0,0,0,0},
		{2,4,8,8,8,4,2}, {8,4,2,2,2,4,8},
		{0,13,18,0,0,0,0}
	};
	if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
	if (ch >= '0' && ch <= '9') return digits[ch - '0'];
	if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
	switch (ch) {
	case ' ': return blank;
	case '!': return punctuation[1];
	case '=': return punctuation[2];
	case ',': return punctuation[3];
	case '/': return punctuation[4];
	case '.': return punctuation[5];
	case '|': return punctuation[6];
	case ':': return punctuation[7];
	case '"': return punctuation[8];
	case '$': return punctuation[9];
	case '?': return punctuation[10];
	case '\'': return extra[0];
	case '#': return extra[1];
	case '%': return extra[2];
	case '&': return extra[3];
	case '(': return extra[4];
	case ')': return extra[5];
	case '*': return extra[6];
	case '+': return extra[7];
	case '-': return extra[8];
	case '<': return extra[9];
	case '>': return extra[10];
	case '[': return extra[11];
	case ']': return extra[12];
	case '_': return extra[13];
	case '^': return extra[14];
	case '@': return extra[15];
	case '\\': return extra[16];
	case '`': return extra[17];
	case '{': return extra[18];
	case '}': return extra[19];
	case '~': return extra[20];
	default: return blank;
	}
}

static unsigned char
vic_screen_code(unsigned char code)
{
	if (code >= 1 && code <= 26)
		return (unsigned char)('A' + code - 1);
	if (code >= 32 && code <= 126)
		return code;
	if (code == 0)
		return '@';
	return ' ';
}

static void
vic_put_pixel(int x, int y, unsigned char color)
{
	SDL_Color c = default_palette[color & 15];
	pixels[(size_t)y * screen_width + x] = 0xFF000000u |
	    ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b;
}

static Uint32
vic_hash_byte(Uint32 hash, unsigned char value)
{
	return (hash ^ value) * 16777619u;
}

void
sdl_vic2_refresh(void)
{
	unsigned char d011, d016, d018;
	unsigned int bank, screen_base, bitmap_base, char_base;
	unsigned int row, column, x, y, cell, cell_address;
	unsigned char code, fg, bg, bits;
	const unsigned char *rows;
	Uint32 hash = 2166136261u;
	int bitmap, extended, custom_chars, reverse;

	if (RAM[SDL_RAM_MODE] != 0)
		return;
	d011 = RAM[0xD011];
	d016 = RAM[0xD016];
	d018 = RAM[0xD018];
	if (!vic2_screen) {
		if (d011 == 0 && d018 == 0 && RAM[0xD020] == 0 &&
		    RAM[0xD021] == 0 && RAM[0x0400] == 0)
			return;
		if (open_screen(320, 200, "cbmbasic VIC-II", 0) != 0)
			return;
		vic2_screen = 1;
	}
	if (d018 == 0)
		d018 = 0x14;

	/* Zero-filled RAM represents the usual bank-zero startup configuration. */
	bank = RAM[0xDD00] == 0 ? 0 :
	    ((~RAM[0xDD00]) & 3u) * 0x4000u;
	screen_base = ((d018 & 0xF0u) >> 4) * 0x400u;
	bitmap_base = (d018 & 0x08u) ? 0x2000u : 0;
	char_base = (d018 & 0x0Eu) << 10;
	bitmap = (d011 & 0x20u) != 0;
	extended = (d011 & 0x40u) != 0 && !bitmap;
	custom_chars = !(bank == 0 &&
	    (char_base == 0x1000u || char_base == 0x1800u));

	hash = vic_hash_byte(hash, d011);
	hash = vic_hash_byte(hash, d016);
	hash = vic_hash_byte(hash, d018);
	for (row = 0; row < 5; row++)
		hash = vic_hash_byte(hash, RAM[0xD020 + row]);
	for (cell = 0; cell < 1000; cell++) {
		hash = vic_hash_byte(hash,
		    RAM[bank + screen_base + cell]);
		if (bitmap)
			continue;
		hash = vic_hash_byte(hash, RAM[0xD800 + cell]);
	}
	if (bitmap) {
		for (cell = 0; cell < 8000; cell++)
			hash = vic_hash_byte(hash,
			    RAM[bank + bitmap_base + cell]);
	} else if (custom_chars) {
		for (cell = 0; cell < 2048; cell++)
			hash = vic_hash_byte(hash,
			    RAM[bank + char_base + cell]);
	}
	if (hash == vic2_hash)
		return;
	vic2_hash = hash;

	bg = RAM[0xD021] & 15;
	for (row = 0; row < 25; row++) {
		for (column = 0; column < 40; column++) {
			cell = row * 40 + column;
			cell_address = bank + screen_base + cell;
			code = RAM[cell_address];
			if (bitmap) {
				unsigned char colors_byte = RAM[cell_address];
				for (y = 0; y < 8; y++) {
					bits = RAM[bank + bitmap_base +
					    (row * 40 + column) * 8 + y];
					for (x = 0; x < 8; x++)
						vic_put_pixel(column * 8 + x,
						    row * 8 + y,
						    (bits & (0x80u >> x)) ?
						    (colors_byte & 15) :
						    (colors_byte >> 4));
				}
				continue;
			}
			if (extended) {
				bg = RAM[0xD021 + (code >> 6)];
				code &= 0x3F;
				reverse = 0;
			} else {
				reverse = (code & 0x80u) != 0;
				code &= 0x7F;
			}
			fg = RAM[0xD800 + cell] & 15;
			for (y = 0; y < 8; y++) {
				if (custom_chars) {
					bits = RAM[bank + char_base +
					    (unsigned int)code * 8 + y];
				} else {
					rows = glyph(vic_screen_code(code));
					bits = y == 0 || y == 7 ? 0 :
					    (unsigned char)(rows[y - 1] << 2);
				}
				for (x = 0; x < 8; x++) {
					int set = (bits & (0x80u >> x)) != 0;
					if (reverse)
						set = !set;
					vic_put_pixel(column * 8 + x, row * 8 + y,
					    set ? fg : bg);
				}
			}
		}
	}
	mark_dirty(0, 0, 320, 200);
	present();
}

int
sdl_text(const char *text, unsigned short x, unsigned short y,
    unsigned short color)
{
	const unsigned char *rows;
	unsigned int column, row;
	int cursor = x, scale = font_scale;

	if (pixels == NULL || color > 255)
		return -1;
	while (*text != '\0') {
		rows = glyph((unsigned char)*text++);
		for (row = 0; row < 7; row++)
			for (column = 0; column < 5; column++)
				if (rows[row] & (1u << (4 - column))) {
					int sx, sy;
					for (sy = 0; sy < scale; sy++)
						for (sx = 0; sx < scale; sx++)
							draw_pixel(cursor + column * scale + sx,
							    y + row * scale + sy, color);
				}
		cursor += 6 * scale;
	}
	finish_draw();
	return 0;
}

void
sdl_line_width(unsigned short width)
{
	stroke_width = width < 1 ? 1 : (width > 64 ? 64 : width);
}

void
sdl_font(const char *name, unsigned short size)
{
	(void)name;
	font_scale = size < 8 ? 1 : (size / 8 > 8 ? 8 : size / 8);
}

void
sdl_alert(unsigned short sound)
{
	static const unsigned short frequencies[] = {440, 523, 587, 659, 784, 880};
	if (sound != 0)
		(void)sdl_beep(frequencies[sound], 100);
}

int
sdl_poll_events(void)
{
	if (window == NULL)
		return 0;
	if (!frame_open && dirty && SDL_GetTicks() - last_present >= 16)
		present();
	else
		poll_events();
	return quit_requested;
}

void
sdl_present(void)
{
	present();
}

void
sdl_sleep(unsigned short milliseconds)
{
	Uint32 until = SDL_GetTicks() + milliseconds;

	while (!quit_requested && (Sint32)(until - SDL_GetTicks()) > 0) {
		sdl_poll_events();
		SDL_Delay(5);
	}
}

int
sdl_get_key(void)
{
	if (window == NULL)
		return -1;
	poll_events();
	if (quit_requested)
		return 27;
	if (pending_key == 0)
		return 0;
	{
		int key = pending_key;
		pending_key = 0;
		RAM[SDL_RAM_KEY] = 0;
		if (RAM[SDL_RAM_MODE] != 0)
			RAM[1024] = 0;
		return key;
	}
}

#else

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

int
sdl_beep(unsigned short frequency, unsigned short duration)
{
	(void)frequency;
	(void)duration;
	fprintf(stderr, "BEEP requires an SDL2-enabled build (make SDL=1)\n");
	return -1;
}

int
sdl_frame(const char *title, const char *message)
{
	(void)title;
	(void)message;
	fprintf(stderr, "FRAME requires an SDL2-enabled build (make SDL=1)\n");
	return -1;
}

int
sdl_frame_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)color; return -1; }
int sdl_screen(unsigned short mode) { (void)mode; return -1; }
int sdl_title(const char *title) { (void)title; return -1; }
int sdl_color(unsigned short i, unsigned short r, unsigned short g,
    unsigned short b, unsigned short a)
{ (void)i; (void)r; (void)g; (void)b; (void)a; return -1; }
int sdl_clear(void) { return -1; }
int sdl_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short c)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)c; return -1; }
int sdl_pixel(unsigned short x, unsigned short y, unsigned short c)
{ (void)x; (void)y; (void)c; return -1; }
int sdl_line(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short c)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)c; return -1; }
int sdl_circle(unsigned short x, unsigned short y, unsigned short r,
    unsigned short c, unsigned short f)
{ (void)x; (void)y; (void)r; (void)c; (void)f; return -1; }
int sdl_ellipse(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short c, unsigned short f)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)c; (void)f; return -1; }
int sdl_arc(unsigned short x, unsigned short y, unsigned short r,
    unsigned short s, unsigned short e, unsigned short c, unsigned short f)
{ (void)x; (void)y; (void)r; (void)s; (void)e; (void)c; (void)f; return -1; }
int sdl_triangle(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short x3, unsigned short y3,
    unsigned short c, unsigned short f)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)x3; (void)y3; (void)c;
  (void)f; return -1; }
int sdl_text(const char *text, unsigned short x, unsigned short y,
    unsigned short c)
{ (void)text; (void)x; (void)y; (void)c; return -1; }
void sdl_line_width(unsigned short width) { (void)width; }
void sdl_font(const char *name, unsigned short size)
{ (void)name; (void)size; }
void sdl_alert(unsigned short sound) { (void)sound; putchar('\a'); }
int sdl_poll_events(void) { return 0; }
void sdl_vic2_refresh(void) {}
void sdl_begin_frame(void) {}
void sdl_end_frame(void) {}
void sdl_present(void) {}
void sdl_sleep(unsigned short milliseconds)
{
#ifdef _WIN32
	Sleep(milliseconds);
#else
	struct timespec delay;
	delay.tv_sec = milliseconds / 1000;
	delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (nanosleep(&delay, &delay) != 0)
		;
#endif
}
int sdl_get_key(void) { return -1; }

#endif
