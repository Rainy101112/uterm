#include <stdint.h>
#include <uterm.h>
#include <buffer.h>
#include <string.h>
#include <stdio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

static uint32_t *uframebuffer;

static ssize_t term_width = 0;		// Terminal width
static ssize_t term_height = 0;		// Terminal height
static uint32_t cell_count = 0;		// The count of all the cells.
static uint32_t cell_cols = 0;		// The count of the cells of col.
static uint32_t cell_lines = 0;		// The count of the cells of line.

static ubuffer_t *front_buffer;
static ubuffer_t *back_buffer;

static vt100_t *vtcontrol;

extern uint8_t ascfont[];

static int cursor_visible = 0;
static uint32_t saved_cursor_cellx, saved_cursor_celly;

void *(*umalloc)(size_t target);	// uterm malloc
void (*ufree)(void *target);		// uterm free

uint32_t cursorx, cursory = 0;

static void swap_buffers(void);
static void uterm_putcursor(void);
static void handle_vt100_command(void);
static void handle_backspace(void);
static uint32_t ansi_to_rgba(int index, int bright);
static void handle_ansi_sgr(void);

static uint32_t ansi_to_rgba(int index, int bright) {
	static const uint32_t base_colors[16] = { // 包含普通和亮色
		0x000000FF, 0xFF0000FF, 0x00FF00FF, 0xFFFF00FF,
		0x0000FFFF, 0xFF00FFFF, 0x00FFFFFF, 0xFFFFFFFF, // 普通色
		0x808080FF, 0xFF8080FF, 0x80FF80FF, 0xFFFF80FF,
		0x8080FFFF, 0xFF80FFFF, 0x80FFFFFF, 0xFFFFFFFF  // 亮色
	};
	return base_colors[index + (bright ? 8 : 0)];
}

static void handle_ansi_sgr() {
	for (int i = 0; i <= vtcontrol->param_count; i++) {
		int code = vtcontrol->params[i];
		switch (code) {
			case 0: // Reset
				vtcontrol->current_fg = ansi_to_rgba(ANSI_COLOR_WHITE, 0);
				vtcontrol->current_bg = ansi_to_rgba(ANSI_COLOR_BLACK, 0);
				break;
			case 30 ... 37:
				vtcontrol->current_fg = ansi_to_rgba(code - 30, 0);
				break;
			case 40 ... 47:
				vtcontrol->current_bg = ansi_to_rgba(code - 40, 0);
				break;
			case 90 ... 97:
				vtcontrol->current_fg = ansi_to_rgba(code - 90, 1);
				break;
			case 100 ... 107:
				vtcontrol->current_bg = ansi_to_rgba(code - 100, 1);
				break;
			default:
				break;
		}
	}
}

static void handle_vt100_command() {
	// 直接访问 params[0] 和 params[1]，避免循环
	int p1 = vtcontrol->params[0];
	int p2 = vtcontrol->params[1];

	switch (vtcontrol->command) {
		// 光标移动
		case 'G': // 水平绝对定位
			cursorx = (p1 > 0) ? MIN(cell_cols-1, p1-1) : 0;
			break;
		case 'A': // 上移
			cursory = (p1 > 0) ? MAX(0, cursory - p1) : MAX(0, cursory - 1);
			break;

		case 'B': // 下移
			cursory = (p1 > 0) ? MIN(cell_lines - 1, cursory + p1) : MIN(cell_lines - 1, cursory + 1);
			break;

		case 'C': // 右移
			cursorx = (p1 > 0) ? MIN(cell_cols - 1, cursorx + p1) : MIN(cell_cols - 1, cursorx + 1);
			break;

		case 'D': // 左移
			cursorx = (p1 > 0) ? MAX(0, cursorx - p1) : MAX(0, cursorx - 1);
			break;

		// 光标定位（行从1开始）
		case 'H':
			cursory = (p1 > 0) ? MIN(cell_lines - 1, p1 - 1) : 0;
			cursorx = (p2 > 0) ? MIN(cell_cols - 1, p2 - 1) : 0;
			break;

		// 清屏
		case 'J':
			if (p1 == 2) { // 清除整个屏幕
				memset(back_buffer->cell, 0, cell_count * sizeof(char));
				// 使用当前背景色填充整个屏幕
				for (size_t i = 0; i < term_width * term_height; ++i) {
				    back_buffer->fb[i] = vtcontrol->current_bg;
				}
				cursorx = cursory = 0;
			}
			break;

		// 清除行
		case 'K':
			if (p1 == 0 || p1 == 1) { // 清除从光标到行尾/行首
				int start = (p1 == 0) ? cursorx : 0;
				int end = (p1 == 0) ? cell_cols : cursorx + 1;
				for (int x = start; x < end; x++) {
					uterm_cell_putc(' ', x, cursory);
				}
			} else if (p1 == 2) { // 清除整行
				for (int x = 0; x < cell_cols; x++) {
					uterm_cell_putc(' ', x, cursory);
				}
			}
			break;
		case 'm':
			handle_ansi_sgr();
			break;
	}

	uterm_show_cursor(1); // 显示新光标
	uterm_putcursor();
}

static void handle_backspace() {
	int original_x = cursorx;
	int original_y = cursory;

	cursorx--;
	if (cursorx < 0) {
		cursory = MAX(0, cursory - 1);
		cursorx = cell_cols - 1;
	}

	if (original_y >= 0 && original_x >= 0) {
		uterm_cell_putc(' ', original_x, original_y); // 使用当前背景色
	}
}

/* Swap buffers */
static void swap_buffers() {
	int start_line = 0, end_line = 0;
	int y_start = 0, y_end = 0;

	if (back_buffer->dirty_start <= back_buffer->dirty_end) {
		start_line =  back_buffer->dirty_start;
		end_line = back_buffer->dirty_end + 1;

		for (int l = start_line; l < end_line; l++) {
			y_start = l * 16;
			y_end = y_start + 16;

			for (int y = y_start; y < y_end; y++) {
				memcpy(
					front_buffer->fb + y * term_width,
					back_buffer->fb + y * term_width,
					term_width * sizeof(uint32_t)
				);
			}
		}

		back_buffer->dirty_start = cell_lines;
		back_buffer->dirty_end = -1;
	}

	front_buffer->cell[saved_cursor_celly * cell_cols + saved_cursor_cellx] = 
		back_buffer->cell[saved_cursor_celly * cell_cols + saved_cursor_cellx];
}

static void uterm_putcursor() {
	if (cursorx >= cell_cols) {
		cursorx = 0;
		cursory++;
		if (cursory >= cell_lines) {
			uterm_scroll();
			cursory = cell_lines - 1;
		}
	}
	// 使用当前背景色作为前景，前景色作为背景来反转光标
	uterm_cell_putc_raw(' ', cursorx, cursory, vtcontrol->current_bg, vtcontrol->current_fg);
}

void uterm_show_cursor(int show) {
	if (cursor_visible) {
		// 恢复原字符颜色到旧光标位置
		char ch = back_buffer->cell[saved_cursor_celly * cell_cols + saved_cursor_cellx];
		uterm_cell_putc(ch, saved_cursor_cellx, saved_cursor_celly);
	}
	if (show) {
		// 保存新位置并绘制反转颜色
		saved_cursor_cellx = cursorx;
		saved_cursor_celly = cursory;
		char ch = back_buffer->cell[cursory * cell_cols + cursorx];
		uterm_cell_putc_raw(ch, cursorx, cursory, vtcontrol->current_bg, vtcontrol->current_fg);
		cursor_visible = 1;
	} else {
		cursor_visible = 0;
	}
}

void init_uterm(uint32_t *vram, ssize_t width, ssize_t height, void *(*malloc)(size_t), void (*free)(void*)){
	cell_cols = width / 8;
	cell_lines = height / 16;
	cell_count = cell_cols * cell_lines;

	term_width = width;
	term_height = height;

	umalloc = malloc;
	ufree = free;

	uframebuffer = vram;

	front_buffer = (ubuffer_t *) umalloc(sizeof(ubuffer_t));
	back_buffer = (ubuffer_t *) umalloc(sizeof(ubuffer_t));

	// 初始化 front_buffer（指向显存）
	front_buffer->fb = uframebuffer;
	front_buffer->cell = (char *) umalloc(cell_lines * cell_cols * sizeof(char));
	memset(front_buffer->cell, 0, cell_lines * cell_cols * sizeof(char));

	vtcontrol = (vt100_t *) umalloc(sizeof(vt100_t)); // 只分配一次

	vtcontrol->current_fg = ansi_to_rgba(ANSI_COLOR_WHITE, 0); // 默认前景色
	vtcontrol->current_bg = ansi_to_rgba(ANSI_COLOR_BLACK, 0); // 默认背景色

	// 初始化 back_buffer（离屏缓冲）
	back_buffer->fb = (uint32_t *) umalloc(term_width * term_height * sizeof(uint32_t));
	back_buffer->cell = (char *) umalloc(cell_lines * cell_cols * sizeof(char));
	memset(back_buffer->fb, 0, term_width * term_height * sizeof(uint32_t));
	memset(back_buffer->cell, 0, cell_lines * cell_cols * sizeof(char));
	back_buffer->dirty_start = 0; // 初始为整个屏幕脏
	back_buffer->dirty_end = cell_lines - 1; // 结束行

	uterm_putcursor();
}

void uterm_draw_pix(int x, int y, uint32_t rgba){
	back_buffer->fb[y * term_width + x] = rgba;

	return;
}

void uterm_cell_putc_raw(char ch, int cellx, int celly, uint32_t rgbaF, uint32_t rgbaB) {
	if (cellx < 0 || cellx >= cell_cols || celly < 0 || celly >= cell_lines) return;

	uint8_t *font = ascfont + ch * 16;
	int start_x = cellx * 8;
	int start_y = celly * 16;

	// 添加对start_x和start_y的边界检查
	if ((start_x+8) > term_width || (start_y+16) > term_height) return;
	back_buffer->cell[celly * cell_cols + cellx] = ch;

	for (int i = 0; i < 16; i++) {
		uint8_t row = font[i];
		uint32_t *fb_row = &back_buffer->fb[(start_y + i) * term_width + start_x];
		fb_row[0] = (row & 0x80) ? rgbaF : rgbaB;
		fb_row[1] = (row & 0x40) ? rgbaF : rgbaB;
		fb_row[2] = (row & 0x20) ? rgbaF : rgbaB;
		fb_row[3] = (row & 0x10) ? rgbaF : rgbaB;
		fb_row[4] = (row & 0x08) ? rgbaF : rgbaB;
		fb_row[5] = (row & 0x04) ? rgbaF : rgbaB;
		fb_row[6] = (row & 0x02) ? rgbaF : rgbaB;
		fb_row[7] = (row & 0x01) ? rgbaF : rgbaB;
	}
}

void uterm_cell_putc(char ch, int cellx, int celly) {
	if (cellx < 0 || cellx >= cell_cols || celly < 0 || celly >= cell_lines) return;

	uint8_t *font = ascfont + ch * 16;
	int start_x = cellx * 8;
	int start_y = celly * 16;

	back_buffer->cell[celly * cell_cols + cellx] = ch;

	// 使用当前颜色设置
	uint32_t rgbaF = vtcontrol->current_fg;
	uint32_t rgbaB = vtcontrol->current_bg;

	// if (vtcontrol->bold) {
	// 	rgbaF = brighten_color(rgbaF);
	// }

	for (int i = 0; i < 16; i++) {
		for (int j = 0; j < 8; j++) {
			int x = start_x + j;
			int y = start_y + i;
			uint32_t color = (font[i] & (0x80 >> j)) ? rgbaF : rgbaB;
			back_buffer->fb[y * term_width + x] = color;
		}
	}

	uterm_cell_putc_raw(ch, cellx, celly, rgbaF, rgbaB); // 直接调用优化版本

	// 更新脏区域
	if (celly < back_buffer->dirty_start)
		back_buffer->dirty_start = celly;
	if (celly > back_buffer->dirty_end)
		back_buffer->dirty_end = celly;
}

void uterm_putc(char ch) {
	uterm_show_cursor(0); // 先隐藏光标

	/* 处理 ANSI 转义序列状态机 */
	if (vtcontrol->status > 0) {
		if (vtcontrol->status == 1) { // 已收到 ESC
			if (ch == '[') {
				vtcontrol->status = 2; // 进入 CSI 模式
				vtcontrol->param_count = 0;
				memset(vtcontrol->params, 0, sizeof(vtcontrol->params));
			} else {
				vtcontrol->status = 0; // 非 CSI 序列，重置
			}
			return; // 处理完 ESC 后立即返回，避免后续逻辑
		}
		else if (vtcontrol->status == 2) { 
			if (ch >= '0' && ch <= '9') {
				vtcontrol->params[vtcontrol->param_count] = vtcontrol->params[vtcontrol->param_count] * 10 + (ch - '0');
			} else if (ch == ';') {
				if (vtcontrol->param_count < 3) {
					vtcontrol->param_count++;
				}
			} else {
				// 处理命令字符
				if (ch == 'm' || ch == 'H' || ch == 'J') { // 仅支持已知命令
					vtcontrol->command = ch;
					handle_vt100_command();
				}
				vtcontrol->status = 0;
				return;
			}
			return; // 确保所有分支返回
		}
	}

	/* 正常字符处理 */
	switch (ch) {
		case '\r':
			cursorx = 0;
			break;

		case '\n':
			cursorx = 0;
			cursory++;
			if (cursory >= cell_lines) {
				uterm_scroll();
				cursory = cell_lines - 1;
			}
			break;

		case '\b':
			handle_backspace();
			break;

		case '\t':
			for (int i = 0; i < 4; i++) uterm_putc(' ');
			break;

		case '\033': // ESC
			vtcontrol->status = 1;
			break;

		default:
			uterm_cell_putc(ch, cursorx, cursory);
			cursorx++;
			if (cursorx >= cell_cols) {
				cursorx = 0;
				cursory++;
				if (cursory >= cell_lines) {
					uterm_scroll();
					cursory = cell_lines - 1;
				}
			}
		}

	uterm_show_cursor(1); // 显示新光标
	uterm_putcursor();
}

void uterm_puts(char *s){
	char c = 0;
	for (; *s; ++s) {
		c = *s;
		uterm_putc(c);
	}
}

void uterm_scroll() {
	memmove(back_buffer->cell, back_buffer->cell + cell_cols, cell_cols * (cell_lines - 1) * sizeof(char));
	memset(back_buffer->cell + cell_cols * (cell_lines - 1), 0, cell_cols * sizeof(char));

	for (int i = 0; i < (cell_lines - 1) * 16; i++) {
		memmove(
			back_buffer->fb + i * term_width,
			back_buffer->fb + (i + 16) * term_width,
			term_width * sizeof(uint32_t)
		);
	}

	// 清除最后 16 行的像素
	for (int y = (cell_lines - 1) * 16; y < cell_lines * 16; y++) {
		for (int x = 0; x < term_width; x++) {
			back_buffer->fb[y * term_width + x] = 0x00000000; // 背景色
		}
	}

	// 标记整个屏幕为脏区域
	back_buffer->dirty_start = 0;
	back_buffer->dirty_end = cell_lines - 1;

	// 批量清除最后16行
	// 清除最后16行的像素，使用当前背景色
	uint32_t *last_lines = back_buffer->fb + (cell_lines - 1) * 16 * term_width;
	for (int i = 0; i < 16 * term_width * 16; ++i) { // 16行高度
		last_lines[i] = vtcontrol->current_bg;
}
}

void uterm_flush(){
	swap_buffers();
	return;
}

void uterm_destroy(){
	ufree(vtcontrol);
	ufree(front_buffer->cell);
	ufree(back_buffer->cell);
	ufree(back_buffer->fb);
	ufree(front_buffer);
	ufree(back_buffer);
	return;
}