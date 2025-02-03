#ifndef INCLUDE_UTERM_H_
#define INCLUDE_UTERM_H_

#include <stdint.h>
#include <stddef.h>
#include <ansi.h>

typedef long ssize_t;

/*
 * @brief FUNCTION DISCRIPTION: Initialize uterm.
 * @param *vram Video memory address. (Frame Buffer)
 * @param width Framebuffer width
 * @param height Framebuffer height
 * @param font[] ASCII font (8x16). Use embedded font if 0. @todo
 * @param malloc System given.
 * @param free System given.
 */
void init_uterm(uint32_t *vram, ssize_t width, ssize_t height, void *(*malloc)(size_t), void (*free)(void*));

void uterm_draw_pix(int x, int y, uint32_t rgba);

void uterm_cell_putc_raw(char ch, int cellx, int celly, uint32_t rgbaF, uint32_t rgbaB);

void uterm_cell_putc(char ch, int cellx, int celly);

void uterm_putc(char ch);

void uterm_puts(char *s);

void uterm_show_cursor(int show);

void uterm_destroy(void);

void uterm_scroll(void);

void uterm_flush(void);

#endif // INCLUDE_UTERM_H_
