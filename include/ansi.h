#ifndef INCLUDE_ANSI_H_
#define INCLUDE_ANSI_H_

#include <stdint.h>

enum ansi_color {
	ANSI_COLOR_BLACK = 0,
	ANSI_COLOR_RED,
	ANSI_COLOR_GREEN,
	ANSI_COLOR_YELLOW,
	ANSI_COLOR_BLUE,
	ANSI_COLOR_MAGENTA,
	ANSI_COLOR_CYAN,
	ANSI_COLOR_WHITE
};

typedef struct vt100 {
	int status;				// 状态机：0-正常，1-收到ESC，2-收到CSI [
	int params[4];			// 参数存储
	int param_count;
	char command;
	uint32_t current_fg;	// 当前前景色（RGBA）
	uint32_t current_bg;	// 当前背景色（RGBA）
	int bold;				// 粗体标志位
	int underline;			// 下划线标志位
} vt100_t;

#endif // INCLUDE_ANSI_H_
