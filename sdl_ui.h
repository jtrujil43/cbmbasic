#ifndef SDL_UI_H_INCLUDED
#define SDL_UI_H_INCLUDED

#define SDL_RAM_WIDTH 0xC000
#define SDL_RAM_HEIGHT 0xC002
#define SDL_RAM_MODE 0xC004
#define SDL_RAM_KEY 0xC005

int sdl_beep(unsigned short frequency, unsigned short duration);
int sdl_frame(const char *title, const char *message);
int sdl_frame_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color);
int sdl_screen(unsigned short mode);
int sdl_title(const char *title);
int sdl_color(unsigned short index, unsigned short red, unsigned short green,
    unsigned short blue, unsigned short alpha);
int sdl_clear(void);
int sdl_rect(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color);
int sdl_pixel(unsigned short x, unsigned short y, unsigned short color);
int sdl_line(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color);
int sdl_circle(unsigned short x, unsigned short y, unsigned short radius,
    unsigned short color, unsigned short fill);
int sdl_ellipse(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short color, unsigned short fill);
int sdl_arc(unsigned short x, unsigned short y, unsigned short radius,
    unsigned short start, unsigned short end, unsigned short color,
    unsigned short fill);
int sdl_triangle(unsigned short x1, unsigned short y1, unsigned short x2,
    unsigned short y2, unsigned short x3, unsigned short y3,
    unsigned short color, unsigned short fill);
int sdl_text(const char *text, unsigned short x, unsigned short y,
    unsigned short color);
void sdl_line_width(unsigned short width);
void sdl_font(const char *name, unsigned short size);
void sdl_alert(unsigned short sound);
int sdl_poll_events(void);
void sdl_vic2_refresh(void);
void sdl_begin_frame(void);
void sdl_end_frame(void);
void sdl_present(void);
void sdl_sleep(unsigned short milliseconds);
int sdl_get_key(void);

#endif
