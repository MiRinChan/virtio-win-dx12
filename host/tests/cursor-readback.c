// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <virglrenderer.h>

struct virgl_box {
    uint32_t x, y, z, w, h, d;
};

enum {
    BGRA_UNORM = 1,
    RGBA_UNORM = 67,
    WIDTH = 64,
    HEIGHT = 64,
};

static void fence(void *cookie, uint32_t id)
{
    (void)cookie;
    (void)id;
}

int main(void)
{
    struct virgl_renderer_callbacks callbacks = {
        .version = 1,
        .write_fence = fence,
    };
    int cookie = 0;
    int ret = virgl_renderer_init(&cookie,
        VIRGL_RENDERER_USE_EGL | VIRGL_RENDERER_USE_SURFACELESS, &callbacks);
    if (ret) {
        printf("init failed %d\n", ret);
        return 2;
    }

    const uint8_t rgba[][4] = {
        {0, 120, 215, 255},
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {40, 80, 120, 128},
    };
    const unsigned formats[] = {BGRA_UNORM, RGBA_UNORM};
    int bad = 0;
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
        unsigned format = formats[f];
        uint8_t pixels[WIDTH * HEIGHT * 4];
        for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
            const uint8_t *color = rgba[(i % WIDTH) % 5];
            pixels[4 * i] = color[format == BGRA_UNORM ? 2 : 0];
            pixels[4 * i + 1] = color[1];
            pixels[4 * i + 2] = color[format == BGRA_UNORM ? 0 : 2];
            pixels[4 * i + 3] = color[3];
        }
        struct iovec iov = {pixels, sizeof(pixels)};
        struct virgl_renderer_resource_create_args args = {
            .handle = format,
            .target = 2,
            .format = format,
            .bind = 2,
            .width = WIDTH,
            .height = HEIGHT,
            .depth = 1,
            .array_size = 1,
            .flags = 1,
        };
        ret = virgl_renderer_resource_create(&args, &iov, 1);
        struct virgl_box box = {0, 0, 0, WIDTH, HEIGHT, 1};
        if (!ret)
            ret = virgl_renderer_transfer_write_iov(format, 0, 0, WIDTH * 4,
                                                    0, &box, 0, &iov, 1);
        uint32_t width = 0, height = 0;
        uint8_t *data = ret ? NULL :
            virgl_renderer_get_cursor_data(format, &width, &height);
        if (!data || width != WIDTH || height != HEIGHT) {
            printf("format %u readback failed %d\n", format, ret);
            bad++;
        } else {
            unsigned errors = 0;
            for (unsigned i = 0; i < WIDTH * HEIGHT; i++)
                if (memcmp(data + 4 * i, rgba[(i % WIDTH) % 5], 4))
                    errors++;
            printf("format %u: first RGBA=%u,%u,%u,%u "
                   "expected=0,120,215,255 mismatches=%u/%u\n",
                   format, data[0], data[1], data[2], data[3], errors,
                   (unsigned)(WIDTH * HEIGHT));
            bad += errors != 0;
        }
        free(data);
        virgl_renderer_resource_unref(format);
    }
    virgl_renderer_cleanup(&cookie);
    return bad ? 1 : 0;
}
