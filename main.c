#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <uterm.h>
#include <string.h>

#define WIDTH  800
#define HEIGHT 600

uint32_t framebuffer[WIDTH * HEIGHT];

// void draw_pixel(int x, int y, uint32_t color) {
//     if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
//         framebuffer[y * WIDTH + x] = color;
//     }
// }

// void draw_rect(int x, int y, int w, int h, uint32_t color) {
//     for (int dy = 0; dy < h; dy++) {
//         for (int dx = 0; dx < w; dx++) {
//             draw_pixel(x + dx, y + dy, color);
//         }
//     }
// }

int main() {
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "无法打开X显示\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen),
        0, 0, WIDTH, HEIGHT, 1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    XSelectInput(display, window, ExposureMask);
    XMapWindow(display, window);

    // 创建图像缓冲区
    XImage* ximage = XCreateImage(
        display, DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap, 0,
        (char*)framebuffer, WIDTH, HEIGHT,
        32, WIDTH * 4
    );

    // 设置正确的字节序
    ximage->byte_order = MSBFirst;
    ximage->bitmap_bit_order = MSBFirst;

    GC gc = XCreateGC(display, window, 0, NULL);

    // 初始化framebuffer为白色
    memset(framebuffer, 0xffffffff, sizeof(framebuffer));
    framebuffer[10 + 10 * WIDTH] = 0x000000;

    init_uterm(framebuffer, WIDTH, HEIGHT, malloc, free);
    
    // uterm_putc('A');
    // uterm_flush();
    // for (int i = 0; i < WIDTH / 8 * HEIGHT / 16 - 300; i++){
    //     uterm_putc('A');
    //     uterm_flush();
    // }

    uterm_putc('H');
    uterm_putc(' ');
    uterm_flush();

    uterm_puts("Hello world");
    uterm_puts("\033[31mHello \033[44mWorld\033[0m\n");
    uterm_flush();

    uterm_puts("UTERM by Rainy101112.");
    uterm_flush();

    // 事件循环
    while (1) {
        XEvent event;
        if (XPending(display)) {
            XNextEvent(display, &event);
            if (event.type == Expose) {
                XPutImage(
                    display, window, gc, ximage,
                    0, 0, 0, 0,
                    WIDTH, HEIGHT
                );
            }
        }

        // 更新显示
        XPutImage(
            display, window, gc, ximage,
            0, 0, 0, 0,
            WIDTH, HEIGHT
        );
        
        usleep(10000);  // 简单帧率控制
    }

    uterm_destroy();
    XDestroyImage(ximage);
    XCloseDisplay(display);
    return 0;
}