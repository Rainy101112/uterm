#ifndef INCLUDE_BUFFER_H_
#define INCLUDE_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

typedef struct ubuffer
{
	uint32_t *fb;
	char *cell;
	int dirty_start; // 起始脏行
	int dirty_end;   // 结束脏行
} ubuffer_t;

typedef struct char_under_cursor
{
	char ch;
	uint32_t rgbaF;
	uint32_t rgbaB;
} cuc_t;

#endif // INCLUDE_BUFFER_H_
