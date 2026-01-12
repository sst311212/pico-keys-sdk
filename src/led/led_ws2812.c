/*
 * This file is part of the Pico Keys SDK distribution (https://github.com/polhenarejos/pico-keys-sdk).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "pico_keys.h"

#ifdef PICO_PLATFORM
#include "hardware/pio.h"
#include "hardware/clocks.h"

#define ws2812_wrap_target 0
#define ws2812_wrap 3
#define ws2812_pio_version 0

#define ws2812_T1 3
#define ws2812_T2 3
#define ws2812_T3 4

static const uint16_t ws2812_program_instructions[] = {
            //     .wrap_target
    0x6321, //  0: out    x, 1            side 0 [3]
    0x1223, //  1: jmp    !x, 3           side 1 [2]
    0x1200, //  2: jmp    0               side 1 [2]
    0xa242, //  3: nop                    side 0 [2]
            //     .wrap
};

static const struct pio_program ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = 4,
    .origin = -1,
    .pio_version = ws2812_pio_version,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0
#endif
};

static inline pio_sm_config ws2812_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + ws2812_wrap_target, offset + ws2812_wrap);
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}

static inline void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq, bool rgbw) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, rgbw ? 32 : 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static void led_driver_init_ws2812(void) {
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    uint8_t gpio = 0;
#ifdef PICO_DEFAULT_WS2812_PIN
    gpio = PICO_DEFAULT_WS2812_PIN;
#endif
    if (phy_data.led_gpio_present) {
        gpio = phy_data.led_gpio;
    }
    ws2812_program_init(pio, sm, offset, gpio, 800000, false);
}

struct urgb_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static struct urgb_color urgb_color_table[] = {
    {0x00, 0x00, 0x00}, // 0: off       LED_COLOR_OFF
    {0xff, 0x00, 0x00}, // 1: red       LED_COLOR_RED
    {0x00, 0xff, 0x00}, // 2: green     LED_COLOR_GREEN
    {0x00, 0x00, 0xff}, // 3: blue      LED_COLOR_BLUE
    {0xff, 0xff, 0x00}, // 4: yellow    LED_COLOR_YELLOW
    {0xff, 0x00, 0xff}, // 5: magenta   LED_COLOR_MAGENTA
    {0x00, 0xff, 0xff}, // 6: cyan      LED_COLOR_CYAN
    {0xff, 0xff, 0xff}  // 7: white     LED_COLOR_WHITE
};

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    if (phy_data.led_driver & PHY_LED_DRIVER_SWAP) {
    #if 1   // TODO: How to adapt WS2812 with different data ordering ?
        return ((uint32_t)(r) << 16) |  // For RGB data ordering WS2812
            ((uint32_t)(g) << 8) |
            (uint32_t)(b);
    #endif
    }
    return ((uint32_t) (r) << 8) |  // For GRB data ordering WS2812
           ((uint32_t) (g) << 16) |
           (uint32_t) (b);
}

static inline void ws2812_put_pixel(uint32_t u32_pixel) {
    pio_sm_put_blocking(pio0, 0, u32_pixel << 8u);
}

static inline void ws2812_rainbow_rgb(struct urgb_color *pixel_color) {
    static uint32_t angle = 0;
    static uint32_t count = 0;
    static uint8_t lights[3][256] = { {
            255, 253, 250, 247, 245, 242, 239, 237, 234, 231, 229, 226, 223, 221, 218, 215, 
            212, 210, 207, 204, 202, 199, 196, 194, 191, 188, 186, 183, 180, 178, 175, 172,
            171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171,
            171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171,
            171, 166, 161, 155, 150, 145, 139, 134, 129, 123, 118, 113, 107, 102, 97 , 91 ,
            86 , 81 , 75 , 70 , 65 , 59 , 54 , 49 , 43 , 38 , 33 , 27 , 22 , 17 , 11 , 6  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 2  , 5  , 8  , 10 , 13 , 16 , 18 , 21 , 24 , 26 , 29 , 32 , 34 , 37 , 40 ,
            43 , 45 , 48 , 51 , 53 , 56 , 59 , 61 , 64 , 67 , 69 , 72 , 75 , 77 , 80 , 83 ,
            85 , 87 , 90 , 93 , 95 , 98 , 101, 103, 106, 109, 111, 114, 117, 119, 122, 125,
            128, 130, 133, 136, 138, 141, 144, 146, 149, 152, 154, 157, 160, 162, 165, 168,
            170, 172, 175, 178, 180, 183, 186, 188, 191, 194, 196, 199, 202, 204, 207, 210,
            213, 215, 218, 221, 223, 226, 229, 231, 234, 237, 239, 242, 245, 247, 250, 253,
        }, {
            0  , 2  , 5  , 8  , 10 , 13 , 16 , 18 , 21 , 24 , 26 , 29 , 32 , 34 , 37 , 40 , 
            43 , 45 , 48 , 51 , 53 , 56 , 59 , 61 , 64 , 67 , 69 , 72 , 75 , 77 , 80 , 83 , 
            85 , 87 , 90 , 93 , 95 , 98 , 101, 103, 106, 109, 111, 114, 117, 119, 122, 125, 
            128, 130, 133, 136, 138, 141, 144, 146, 149, 152, 154, 157, 160, 162, 165, 168, 
            170, 172, 175, 178, 180, 183, 186, 188, 191, 194, 196, 199, 202, 204, 207, 210, 
            213, 215, 218, 221, 223, 226, 229, 231, 234, 237, 239, 242, 245, 247, 250, 253, 
            255, 253, 250, 247, 245, 242, 239, 237, 234, 231, 229, 226, 223, 221, 218, 215, 
            212, 210, 207, 204, 202, 199, 196, 194, 191, 188, 186, 183, 180, 178, 175, 172, 
            171, 166, 161, 155, 150, 145, 139, 134, 129, 123, 118, 113, 107, 102, 97 , 91 , 
            86 , 81 , 75 , 70 , 65 , 59 , 54 , 49 , 43 , 38 , 33 , 27 , 22 , 17 , 11 , 6  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
        }, {
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
            0  , 2  , 5  , 8  , 10 , 13 , 16 , 18 , 21 , 24 , 26 , 29 , 32 , 34 , 37 , 40 ,
            43 , 45 , 48 , 51 , 53 , 56 , 59 , 61 , 64 , 67 , 69 , 72 , 75 , 77 , 80 , 83 ,
            85 , 90 , 95 , 101, 106, 111, 117, 122, 127, 133, 138, 143, 149, 154, 159, 165,
            170, 175, 181, 186, 191, 197, 202, 207, 213, 218, 223, 229, 234, 239, 245, 250,
            255, 253, 250, 247, 245, 242, 239, 237, 234, 231, 229, 226, 223, 221, 218, 215,
            212, 210, 207, 204, 202, 199, 196, 194, 191, 188, 186, 183, 180, 178, 175, 172,
            171, 169, 166, 163, 161, 158, 155, 153, 150, 147, 145, 142, 139, 137, 134, 131,
            128, 126, 123, 120, 118, 115, 112, 110, 107, 104, 102, 99 , 96 , 94 , 91 , 88 ,
            85 , 83 , 80 , 77 , 75 , 72 , 69 , 67 , 64 , 61 , 59 , 56 , 53 , 51 , 48 , 45 ,
            42 , 40 , 37 , 34 , 32 , 29 , 26 , 24 , 21 , 18 , 16 , 13 , 10 , 8  , 5  , 2  ,
        }
    };
    pixel_color->r = lights[0][angle % 256];
    pixel_color->g = lights[1][angle % 256];
    pixel_color->b = lights[2][angle % 256];
    angle += ((++count % 15) == 0) ? 1 : 0;
}

static void led_driver_color_ws2812(uint8_t color, uint32_t led_brightness, float progress) {
    if (!(phy_data.opts & PHY_OPT_DIMM)) {
        progress = progress >= 0.5 ? 1 : 0;
    }
    uint32_t led_phy_btness = phy_data.led_brightness_present ? phy_data.led_brightness : MAX_BTNESS;

    float brightness = ((float)led_brightness / MAX_BTNESS) * ((float)led_phy_btness / MAX_BTNESS) * progress;
    struct urgb_color pixel_color = urgb_color_table[color];
    
    #include <led/led.h>
    uint32_t led_mode = led_get_mode();
    if (phy_data.opts & PHY_OPT_LED_RAINBOW) {
        if (led_mode == MODE_MOUNTED || led_mode == MODE_SUSPENDED) {
            ws2812_rainbow_rgb(&pixel_color);
        }
    }

    pixel_color.r = (uint8_t)(pixel_color.r * brightness);
    pixel_color.g = (uint8_t)(pixel_color.g * brightness);
    pixel_color.b = (uint8_t)(pixel_color.b * brightness);

    ws2812_put_pixel(urgb_u32(pixel_color.r, pixel_color.g, pixel_color.b));
}

led_driver_t led_driver_ws2812 = {
    .init = led_driver_init_ws2812,
    .set_color = led_driver_color_ws2812,
};

#endif
