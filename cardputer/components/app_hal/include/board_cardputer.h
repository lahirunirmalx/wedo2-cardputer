// SPDX-License-Identifier: AGPL-3.0-or-later
// board_cardputer.h - pin map of the M5Stack Cardputer (original / v1.1,
// StampS3: ESP32-S3FN8, 8 MB flash, no PSRAM).
//
// Sources: M5Stack M5Cardputer library (keyboard matrix) and the Braid
// demo's cardputer build (LCD, run on this hardware).
//
// ESP32-S3 strapping pins are GPIO0, GPIO3, GPIO45 and GPIO46. Flash SPI
// uses GPIO26..32. Only GPIO3 appears below.

#pragma once

#include <stdint.h>

/* ST7789V2 1.14 inch IPS, 135 x 240, used landscape (240 x 135). */
#define BOARD_LCD_SCK    36
#define BOARD_LCD_MOSI   35
#define BOARD_LCD_CS     37
#define BOARD_LCD_DC     34
#define BOARD_LCD_RST    33
#define BOARD_LCD_BL     38
#define BOARD_LCD_HZ     40000000u
#define BOARD_LCD_W      240u
#define BOARD_LCD_H      135u
#define BOARD_LCD_X_OFF  40u    /* panel RAM offset of the visible window */
#define BOARD_LCD_Y_OFF  53u
#define BOARD_LCD_MADCTL 0x60u  /* MX | MV: landscape, RGB order */

/* Speaker: NS4168 I2S amplifier (M5Unified: bck 41, ws 43, data 42).
 * GPIO43 is also UART0 TX, the default console pin: the I2S driver takes
 * the pad over; logs still reach the USB-Serial/JTAG console. */
#define BOARD_SPK_BCLK   41
#define BOARD_SPK_WS     43
#define BOARD_SPK_DOUT   42

/* Keyboard: 74HC138 decoder selects one of 8 lines, 7 inputs read back. */
#define BOARD_KBD_ADDR_PINS  {8u, 9u, 11u}
/* STRAPPING: GPIO3 selects the JTAG source at reset, but only when the
 * efuse STRAP_JTAG_SEL is burned (it is not). Used as a pulled-up input
 * only; pressing a key at reset does not change the boot mode. */
#define BOARD_KBD_INPUT_PINS {13u, 15u, 3u, 4u, 5u, 6u, 7u}
#define BOARD_KBD_ADDR_COUNT  3u
#define BOARD_KBD_INPUT_COUNT 7u
