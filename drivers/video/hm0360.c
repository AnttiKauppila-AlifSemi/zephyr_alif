/*
 * Copyright (c) 2025 Alif Semiconductor.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT himax_hm0360

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(hm0360);

#include <zephyr/drivers/video/hm0360-video-controls.h>

/* Register definitions */
#define HM0360_CHIP_ID_VAL             0x0360
#define HM0360_CHIP_ID_H_REGISTER      0x0000
#define HM0360_CHIP_ID_L_REGISTER      0x0001
#define HM0360_MODE_SELECT_REGISTER    0x0100
#define HM0360_SOFTWARE_RESET_REGISTER 0x0103
#define HM0360_PMU_CFG_7               0x3028 /* Output frame count */

/* Mode select bitfields */
enum hm0360_mode {
	HM0360_MODE_I2C_TRIGGER_SLEEP             = 0x00,
	HM0360_MODE_I2C_TRIGGER_CONT_STREAMING    = 0x01,
	HM0360_MODE_I2C_TRIGGER_AUTO_WAKEUP       = 0x02,
	HM0360_MODE_I2C_TRIGGER_SNAPSHOT_N_FRAMES = 0x03,
	HM0360_MODE_HW_TRIGGER_SLEEP              = 0x04,
	HM0360_MODE_HW_TRIGGER_CONT_STREAMING     = 0x05,
	HM0360_MODE_HW_TRIGGER_AUTO_WAKEUP        = 0x06,
	HM0360_MODE_HW_TRIGGER_SNAPSHOT_N_FRAMES  = 0x07
};

/* Data structures */
struct hm0360_reg {
	uint16_t addr;
	uint8_t value;
};

struct hm0360_config {
	struct i2c_dt_spec i2c;
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct gpio_dt_spec resetn_gpio;	/* Active low Reset (XSHUTDOWN) */
#endif
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
	const struct gpio_dt_spec power_gpio;
#endif
#if DT_INST_NODE_HAS_PROP(0, xsleep_gpios)
	const struct gpio_dt_spec xsleepn_gpio; /* Active low LP Sleep mode (XSLEEP) */
#endif
	uint8_t fps;
};

enum capture_type {
	CONTINUOUS_CAPTURE,
	SNAPSHOT_CAPTURE,
};

struct hm0360_data {
	struct video_format fmt;
	enum capture_type capture_mode;
	uint8_t numframes;
};

struct hm0360_resolution_config {
	uint16_t width;
	uint16_t height;
	uint8_t fps;
	uint16_t size_params;
	const struct hm0360_reg *params;
};

/* Helper macro for video format capabilities */
#define HM0360_VIDEO_FORMAT_CAP(w, h, f) { \
	.pixelformat = (f), .width_min = (w), .width_max = (w), \
	.height_min = (h), .height_max = (h), .width_step = 0, .height_step = 0 \
}

/* Resolution configurations */
static const struct hm0360_reg hm0360_320x240_60fps[] = {
	{ 0x3024, 0x01 }, /* Context-A, I2C trigger */
	{ 0x3588, 0x02 }, /* Frame_rate1_H */
	{ 0x3589, 0x12 }, /* frame_rate1_L */
	{ 0x358A, 0x04 }, /* frame_rate2_H */
	{ 0x358B, 0x24 }, /* frame_rate2_L */
	{ 0x358C, 0x06 }, /* frame_rate3_H */
	{ 0x358D, 0x36 }, /* frame_rate3_L */
};

static const struct hm0360_reg hm0360_640x480_60fps[] = {
	{ 0x3024, 0x00 }, /* Context-A, I2C trigger */
	{ 0x3030, 0x01 },
	{ 0x352E, 0x02 }, /* Frame_rate1_H */
	{ 0x352F, 0x14 }, /* frame_rate1_L */
	{ 0x3530, 0x04 }, /* frame_rate2_H */
	{ 0x3531, 0x10 }, /* frame_rate2_L */
	{ 0x3532, 0x06 }, /* frame_rate3_H */
	{ 0x3533, 0x1A }, /* frame_rate3_L */
};

static const struct hm0360_resolution_config resolution_configs[] = {
	{
		.width = 320, .height = 240, .fps = 60,
		.params = hm0360_320x240_60fps,
		.size_params = ARRAY_SIZE(hm0360_320x240_60fps)
	},
	{
		.width = 640, .height = 480, .fps = 60,
		.params = hm0360_640x480_60fps,
		.size_params = ARRAY_SIZE(hm0360_640x480_60fps)
	},
};

static const struct video_format_cap fmts[] = {
	HM0360_VIDEO_FORMAT_CAP(320, 240, VIDEO_PIX_FMT_BGGR8),
	HM0360_VIDEO_FORMAT_CAP(640, 480, VIDEO_PIX_FMT_BGGR8),
	{0},
};

/* Initialization configuration */
static const struct hm0360_reg hm0360_init_config[] = {
	{ 0x0102, 0x00 },
	{ 0x0350, 0xE0 },
	{ 0x0370, 0x00 },
	{ 0x0371, 0x00 },
	{ 0x0372, 0x01 },
	{ 0x1000, 0x43 },
	{ 0x1001, 0x80 },
	{ 0x1003, 0x20 },
	{ 0x1004, 0x20 },
	{ 0x1007, 0x01 },
	{ 0x1008, 0x20 },
	{ 0x1009, 0x20 },
	{ 0x100A, 0x05 },
	{ 0x100B, 0x20 },
	{ 0x100C, 0x20 },
	{ 0x1013, 0x00 },
	{ 0x1014, 0x00 },
	{ 0x1018, 0x00 },
	{ 0x101D, 0xCF },
	{ 0x101E, 0x01 },
	{ 0x101F, 0x00 },
	{ 0x1020, 0x01 },
	{ 0x1021, 0x5D },
	{ 0x102F, 0x08 },
	{ 0x1030, 0x04 },
	{ 0x1031, 0x08 },
	{ 0x1032, 0x10 },
	{ 0x1033, 0x18 },
	{ 0x1034, 0x20 },
	{ 0x1035, 0x28 },
	{ 0x1036, 0x30 },
	{ 0x1037, 0x38 },
	{ 0x1038, 0x40 },
	{ 0x1039, 0x50 },
	{ 0x103A, 0x60 },
	{ 0x103B, 0x70 },
	{ 0x103C, 0x80 },
	{ 0x103D, 0xA0 },
	{ 0x103E, 0xC0 },
	{ 0x103F, 0xE0 },
	{ 0x1041, 0x00 },
	{ 0x2000, 0x7F },
	{ 0x202B, 0x04 },
	{ 0x202C, 0x03 },
	{ 0x202D, 0x00 },
	{ 0x2031, 0x60 },
	{ 0x2032, 0x08 },
	{ 0x2034, 0x70 },
	{ 0x2036, 0x19 },
	{ 0x2037, 0x08 },
	{ 0x2038, 0x10 },
	{ 0x203C, 0x01 },
	{ 0x203D, 0x04 },
	{ 0x203E, 0x01 },
	{ 0x203F, 0x38 },
	{ 0x2048, 0x00 },
	{ 0x2049, 0x10 },
	{ 0x204A, 0x40 },
	{ 0x204B, 0x00 },
	{ 0x204C, 0x08 },
	{ 0x204D, 0x20 },
	{ 0x204E, 0x00 },
	{ 0x204F, 0x38 },
	{ 0x2050, 0xE0 },
	{ 0x2051, 0x00 },
	{ 0x2052, 0x1C },
	{ 0x2053, 0x70 },
	{ 0x2054, 0x00 },
	{ 0x2055, 0x1A },
	{ 0x2056, 0xC0 },
	{ 0x2057, 0x00 },
	{ 0x2058, 0x06 },
	{ 0x2059, 0xB0 },
	{ 0x2061, 0x00 },
	{ 0x2062, 0x00 },
	{ 0x2063, 0xC8 },
	{ 0x2080, 0x41 },
	{ 0x2081, 0xE0 },
	{ 0x2082, 0xF0 },
	{ 0x2083, 0x01 },
	{ 0x2084, 0x10 },
	{ 0x2085, 0x10 },
	{ 0x2086, 0x01 },
	{ 0x2087, 0x06 },
	{ 0x2088, 0x0C },
	{ 0x2089, 0x12 },
	{ 0x208A, 0x1C },
	{ 0x208B, 0x30 },
	{ 0x208C, 0x10 },
	{ 0x208D, 0x02 },
	{ 0x208E, 0x08 },
	{ 0x208F, 0x0D },
	{ 0x2090, 0x14 },
	{ 0x2091, 0x1D },
	{ 0x2092, 0x30 },
	{ 0x2093, 0x08 },
	{ 0x2094, 0x0A },
	{ 0x2095, 0x0F },
	{ 0x2096, 0x14 },
	{ 0x2097, 0x18 },
	{ 0x2098, 0x20 },
	{ 0x2099, 0x10 },
	{ 0x209A, 0x00 },
	{ 0x209B, 0x01 },
	{ 0x209C, 0x01 },
	{ 0x209D, 0x11 },
	{ 0x209E, 0x06 },
	{ 0x209F, 0x20 },
	{ 0x20A0, 0x10 },
	{ 0x2590, 0x01 },
	{ 0x2800, 0x09 },
	{ 0x2804, 0x02 },
	{ 0x2805, 0x03 },
	{ 0x2806, 0x03 },
	{ 0x2807, 0x08 },
	{ 0x2808, 0x04 },
	{ 0x2809, 0x0C },
	{ 0x280A, 0x03 },
	{ 0x280F, 0x03 },
	{ 0x2810, 0x03 },
	{ 0x2811, 0x00 },
	{ 0x2812, 0x09 },
	{ 0x2821, 0xEE },
	{ 0x282A, 0x0F },
	{ 0x282B, 0x08 },
	{ 0x282E, 0x2F },
	{ 0x3010, 0x00 },
	{ 0x3013, 0x01 },
	{ 0x3019, 0x00 },
	{ 0x301A, 0x00 },
	{ 0x301B, 0x20 },
	{ 0x301C, 0xFF },
	{ 0x3020, 0x00 },
	{ 0x3021, 0x00 },
	{ 0x3024, 0x00 },
	{ 0x3025, 0x12 },
	{ 0x3026, 0x03 },
	{ 0x3027, 0x81 },
	{ 0x3028, 0x01 },
	{ 0x3029, 0x15 },
	{ 0x302A, 0x60 },
	{ 0x302B, 0x2A },
	{ 0x302C, 0x00 },
	{ 0x302D, 0x03 },
	{ 0x302E, 0x00 },
	{ 0x302F, 0x00 },
	{ 0x3031, 0x01 },
	{ 0x3034, 0x00 },
	{ 0x3035, 0x01 },
	{ 0x3051, 0x00 },
	{ 0x305C, 0x03 },
	{ 0x3060, 0x00 },
	{ 0x3061, 0xFA },
	{ 0x3062, 0xFF },
	{ 0x3063, 0xFF },
	{ 0x3064, 0xFF },
	{ 0x3065, 0xFF },
	{ 0x3066, 0xFF },
	{ 0x3067, 0xFF },
	{ 0x3068, 0xFF },
	{ 0x3069, 0xFF },
	{ 0x306A, 0xFF },
	{ 0x306B, 0xFF },
	{ 0x306C, 0xFF },
	{ 0x306D, 0xFF },
	{ 0x306E, 0xFF },
	{ 0x306F, 0xFF },
	{ 0x3070, 0xFF },
	{ 0x3071, 0xFF },
	{ 0x3072, 0xFF },
	{ 0x3073, 0xFF },
	{ 0x3074, 0xFF },
	{ 0x3075, 0xFF },
	{ 0x3076, 0xFF },
	{ 0x3077, 0xFF },
	{ 0x3078, 0xFF },
	{ 0x3079, 0xFF },
	{ 0x307A, 0xFF },
	{ 0x307B, 0xFF },
	{ 0x307C, 0xFF },
	{ 0x307D, 0xFF },
	{ 0x307E, 0xFF },
	{ 0x307F, 0xFF },
	{ 0x3080, 0x00 },
	{ 0x3081, 0x00 },
	{ 0x3082, 0x00 },
	{ 0x3083, 0x20 },
	{ 0x3084, 0x00 },
	{ 0x3085, 0x20 },
	{ 0x3086, 0x00 },
	{ 0x3087, 0x20 },
	{ 0x3088, 0x00 },
	{ 0x3089, 0x04 },
	{ 0x3094, 0x02 },
	{ 0x3095, 0x02 },
	{ 0x3096, 0x00 },
	{ 0x3097, 0x02 },
	{ 0x3098, 0x00 },
	{ 0x3099, 0x02 },
	{ 0x309E, 0x05 },
	{ 0x309F, 0x02 },
	{ 0x30A0, 0x02 },
	{ 0x30A1, 0x00 },
	{ 0x30A2, 0x08 },
	{ 0x30A3, 0x00 },
	{ 0x30A4, 0x20 },
	{ 0x30A5, 0x04 },
	{ 0x30A6, 0x02 },
	{ 0x30A7, 0x02 },
	{ 0x30A8, 0x02 },
	{ 0x30A9, 0x00 },
	{ 0x30AA, 0x02 },
	{ 0x30AB, 0x34 },
	{ 0x30B0, 0x03 },
	{ 0x30C4, 0x10 },
	{ 0x30C5, 0x01 },
	{ 0x30C6, 0x2F },
	{ 0x30C7, 0x00 },
	{ 0x30C8, 0x00 },
	{ 0x30CB, 0xFF },
	{ 0x30CC, 0xFF },
	{ 0x30CD, 0x7F },
	{ 0x30CE, 0x7F },
	{ 0x30D3, 0x01 },
	{ 0x30D4, 0xFF },
	{ 0x30D5, 0x00 },
	{ 0x30D6, 0x40 },
	{ 0x30D7, 0x00 },
	{ 0x30D8, 0xA7 },
	{ 0x30D9, 0x00 },
	{ 0x30DA, 0x01 },
	{ 0x30DB, 0x40 },
	{ 0x30DC, 0x00 },
	{ 0x30DD, 0x27 },
	{ 0x30DE, 0x05 },
	{ 0x30DF, 0x07 },
	{ 0x30E0, 0x40 },
	{ 0x30E1, 0x00 },
	{ 0x30E2, 0x27 },
	{ 0x30E3, 0x05 },
	{ 0x30E4, 0x47 },
	{ 0x30E5, 0x30 },
	{ 0x30E6, 0x00 },
	{ 0x30E7, 0x27 },
	{ 0x30E8, 0x05 },
	{ 0x30E9, 0x87 },
	{ 0x30EA, 0x30 },
	{ 0x30EB, 0x00 },
	{ 0x30EC, 0x27 },
	{ 0x30ED, 0x05 },
	{ 0x30EE, 0x00 },
	{ 0x30EF, 0x40 },
	{ 0x30F0, 0x00 },
	{ 0x30F1, 0xA7 },
	{ 0x30F2, 0x00 },
	{ 0x30F3, 0x01 },
	{ 0x30F4, 0x40 },
	{ 0x30F5, 0x00 },
	{ 0x30F6, 0x27 },
	{ 0x30F7, 0x05 },
	{ 0x30F8, 0x07 },
	{ 0x30F9, 0x40 },
	{ 0x30FA, 0x00 },
	{ 0x30FB, 0x27 },
	{ 0x30FC, 0x05 },
	{ 0x30FD, 0x47 },
	{ 0x30FE, 0x30 },
	{ 0x30FF, 0x00 },
	{ 0x3100, 0x27 },
	{ 0x3101, 0x05 },
	{ 0x3102, 0x87 },
	{ 0x3103, 0x30 },
	{ 0x3104, 0x00 },
	{ 0x3105, 0x27 },
	{ 0x3106, 0x05 },
	{ 0x310B, 0x10 },
	{ 0x3112, 0x04 },
	{ 0x3113, 0xA0 },
	{ 0x3114, 0x67 },
	{ 0x3115, 0x42 },
	{ 0x3116, 0x10 },
	{ 0x3117, 0x0A },
	{ 0x3118, 0x3F },
	{ 0x311A, 0x30 },
	{ 0x311C, 0x10 },
	{ 0x311D, 0x06 },
	{ 0x311E, 0x0F },
	{ 0x311F, 0x0E },
	{ 0x3120, 0x0D },
	{ 0x3121, 0x0F },
	{ 0x3122, 0x00 },
	{ 0x3123, 0x1D },
	{ 0x3126, 0x03 },
	{ 0x3127, 0xC4 },
	{ 0x3128, 0x57 },
	{ 0x312A, 0x11 },
	{ 0x312B, 0x41 },
	{ 0x312E, 0x00 },
	{ 0x312F, 0x00 },
	{ 0x3130, 0x0C },
	{ 0x3141, 0x2A },
	{ 0x3142, 0x9F },
	{ 0x3147, 0x18 },
	{ 0x3149, 0x28 },
	{ 0x314B, 0x01 },
	{ 0x3150, 0x50 },
	{ 0x3152, 0x00 },
	{ 0x3156, 0x2C },
	{ 0x315A, 0x0A },
	{ 0x315B, 0x2F },
	{ 0x315C, 0xE0 },
	{ 0x315F, 0x02 },
	{ 0x3160, 0x1F },
	{ 0x3163, 0x1F },
	{ 0x3164, 0x7F },
	{ 0x3165, 0x7F },
	{ 0x317B, 0x94 },
	{ 0x317C, 0x00 },
	{ 0x317D, 0x02 },
	{ 0x318C, 0x00 },
	{ 0x3500, 0x78 },
	{ 0x3501, 0x0A },
	{ 0x3502, 0x77 },
	{ 0x3503, 0x02 },
	{ 0x3504, 0x14 },
	{ 0x3505, 0x03 },
	{ 0x3506, 0x00 },
	{ 0x3507, 0x00 },
	{ 0x3508, 0x00 },
	{ 0x3509, 0x00 },
	{ 0x350A, 0xFF },
	{ 0x350B, 0x00 },
	{ 0x350C, 0x00 },
	{ 0x350D, 0x01 },
	{ 0x350F, 0x00 },
	{ 0x3510, 0x02 },
	{ 0x3511, 0x00 },
	{ 0x3512, 0x7F },
	{ 0x3513, 0x00 },
	{ 0x3514, 0x00 },
	{ 0x3515, 0x01 },
	{ 0x3516, 0x00 },
	{ 0x3517, 0x02 },
	{ 0x3518, 0x00 },
	{ 0x3519, 0x7F },
	{ 0x351A, 0x00 },
	{ 0x351B, 0x5F },
	{ 0x351C, 0x00 },
	{ 0x351D, 0x02 },
	{ 0x351E, 0x10 },
	{ 0x351F, 0x04 },
	{ 0x3520, 0x03 },
	{ 0x3521, 0x00 },
	{ 0x3523, 0x60 },
	{ 0x3524, 0x08 },
	{ 0x3525, 0x19 },
	{ 0x3526, 0x08 },
	{ 0x3527, 0x10 },
	{ 0x352A, 0x01 },
	{ 0x352B, 0x04 },
	{ 0x352C, 0x01 },
	{ 0x352D, 0x39 },
	{ 0x3535, 0x02 },
	{ 0x3536, 0x03 },
	{ 0x3537, 0x03 },
	{ 0x3538, 0x08 },
	{ 0x3539, 0x04 },
	{ 0x353A, 0x0C },
	{ 0x353B, 0x03 },
	{ 0x3540, 0x03 },
	{ 0x3541, 0x03 },
	{ 0x3542, 0x00 },
	{ 0x3543, 0x09 },
	{ 0x3549, 0x04 },
	{ 0x354A, 0x35 },
	{ 0x354B, 0x21 },
	{ 0x354C, 0x01 },
	{ 0x354D, 0xE0 },
	{ 0x354E, 0xF0 },
	{ 0x354F, 0x10 },
	{ 0x3550, 0x10 },
	{ 0x3551, 0x10 },
	{ 0x3552, 0x20 },
	{ 0x3553, 0x10 },
	{ 0x3554, 0x01 },
	{ 0x3555, 0x06 },
	{ 0x3556, 0x0C },
	{ 0x3557, 0x12 },
	{ 0x3558, 0x1C },
	{ 0x3559, 0x30 },
	{ 0x355A, 0x78 },
	{ 0x355B, 0x0A },
	{ 0x355C, 0x77 },
	{ 0x355D, 0x01 },
	{ 0x355E, 0x1C },
	{ 0x355F, 0x03 },
	{ 0x3560, 0x00 },
	{ 0x3561, 0x01 },
	{ 0x3562, 0x01 },
	{ 0x3563, 0x00 },
	{ 0x3564, 0xFF },
	{ 0x3565, 0x00 },
	{ 0x3566, 0x00 },
	{ 0x3567, 0x01 },
	{ 0x3569, 0x00 },
	{ 0x356A, 0x02 },
	{ 0x356B, 0x00 },
	{ 0x356C, 0x7F },
	{ 0x356D, 0x00 },
	{ 0x356E, 0x00 },
	{ 0x356F, 0x01 },
	{ 0x3570, 0x00 },
	{ 0x3571, 0x02 },
	{ 0x3572, 0x00 },
	{ 0x3573, 0x3F },
	{ 0x3574, 0x00 },
	{ 0x3575, 0x2F },
	{ 0x3576, 0x00 },
	{ 0x3577, 0x02 },
	{ 0x3578, 0x24 },
	{ 0x3579, 0x04 },
	{ 0x357A, 0x03 },
	{ 0x357B, 0x00 },
	{ 0x357D, 0x60 },
	{ 0x357E, 0x08 },
	{ 0x357F, 0x19 },
	{ 0x3580, 0x08 },
	{ 0x3581, 0x10 },
	{ 0x3584, 0x01 },
	{ 0x3585, 0x04 },
	{ 0x3586, 0x01 },
	{ 0x3587, 0x39 },
	{ 0x3588, 0x02 },
	{ 0x3589, 0x12 },
	{ 0x358A, 0x04 },
	{ 0x358B, 0x24 },
	{ 0x358C, 0x06 },
	{ 0x358D, 0x36 },
	{ 0x358F, 0x02 },
	{ 0x3590, 0x03 },
	{ 0x3591, 0x03 },
	{ 0x3592, 0x08 },
	{ 0x3593, 0x04 },
	{ 0x3594, 0x0C },
	{ 0x3595, 0x03 },
	{ 0x359A, 0x03 },
	{ 0x359B, 0x03 },
	{ 0x359C, 0x00 },
	{ 0x359D, 0x09 },
	{ 0x35A3, 0x02 },
	{ 0x35A4, 0x03 },
	{ 0x35A5, 0x21 },
	{ 0x35A6, 0x01 },
	{ 0x35A7, 0xE0 },
	{ 0x35A8, 0xF0 },
	{ 0x35A9, 0x10 },
	{ 0x35AA, 0x10 },
	{ 0x35AB, 0x10 },
	{ 0x35AC, 0x20 },
	{ 0x35AD, 0x10 },
	{ 0x35AE, 0x01 },
	{ 0x35AF, 0x06 },
	{ 0x35B0, 0x0C },
	{ 0x35B1, 0x12 },
	{ 0x35B2, 0x1C },
	{ 0x35B3, 0x30 },
	{ 0x35B4, 0x78 },
	{ 0x35B5, 0x0A },
	{ 0x35B6, 0x77 },
	{ 0x35B7, 0x00 },
	{ 0x35B8, 0x94 },
	{ 0x35B9, 0x03 },
	{ 0x35BA, 0x00 },
	{ 0x35BB, 0x03 },
	{ 0x35BC, 0x03 },
	{ 0x35BD, 0x00 },
	{ 0x35BE, 0xFF },
	{ 0x35BF, 0x00 },
	{ 0x35C0, 0x01 },
	{ 0x35C1, 0x01 },
	{ 0x35C3, 0x00 },
	{ 0x35C4, 0x00 },
	{ 0x35C5, 0x00 },
	{ 0x35C6, 0x7F },
	{ 0x35C7, 0x00 },
	{ 0x35C8, 0x00 },
	{ 0x35C9, 0x01 },
	{ 0x35CA, 0x00 },
	{ 0x35CB, 0x02 },
	{ 0x35CC, 0x00 },
	{ 0x35CD, 0x0F },
	{ 0x35CE, 0x00 },
	{ 0x35CF, 0x0B },
	{ 0x35D0, 0x00 },
	{ 0x35D3, 0x04 },
	{ 0x35D7, 0x18 },
	{ 0x35D8, 0x01 },
	{ 0x35D9, 0x20 },
	{ 0x35DA, 0x08 },
	{ 0x35DB, 0x14 },
	{ 0x35DC, 0x70 },
	{ 0x35DE, 0x00 },
	{ 0x35DF, 0x01 },
	{ 0x35E9, 0x02 },
	{ 0x35EA, 0x03 },
	{ 0x35EB, 0x03 },
	{ 0x35EC, 0x08 },
	{ 0x35ED, 0x04 },
	{ 0x35EE, 0x0C },
	{ 0x35EF, 0x03 },
	{ 0x35F4, 0x03 },
	{ 0x35F5, 0x03 },
	{ 0x35F6, 0x00 },
	{ 0x35F7, 0x09 },
	{ 0x35FD, 0x00 },
	{ 0x35FE, 0x5E },
	{ 0x0104, 0x01 },
	{ 0x0100, 0x01 },
};

/* I2C Helper Functions */
static int hm0360_burst_write(const struct device *dev, uint16_t start_addr,
		const uint8_t *buf, uint32_t num_bytes)
{
	const struct hm0360_config *cfg = dev->config;
	uint8_t addr_buffer[2];
	struct i2c_msg msg[2];

	addr_buffer[0] = start_addr >> 8;
	addr_buffer[1] = start_addr & 0xFF;

	msg[0].buf = addr_buffer;
	msg[0].len = 2;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = (uint8_t *)buf;
	msg[1].len = num_bytes;
	msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, msg, 2);
}

static int hm0360_read_reg(const struct device *dev, uint16_t addr, uint8_t *val)
{
	const struct hm0360_config *cfg = dev->config;
	uint8_t addr_buffer[2];

	addr_buffer[0] = addr >> 8;
	addr_buffer[1] = addr & 0xFF;

	return i2c_write_read_dt(&cfg->i2c, addr_buffer, sizeof(addr_buffer), val, 1);
}

static int hm0360_write_reg(const struct device *dev, uint16_t addr, uint8_t val)
{
	return hm0360_burst_write(dev, addr, &val, 1);
}

static int hm0360_write_regs(const struct device *dev, const struct hm0360_reg *regs, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		int ret = hm0360_write_reg(dev, regs[i].addr, regs[i].value);

		if (ret) {
			LOG_ERR("Failed to write reg 0x%04x: %d", regs[i].addr, ret);
			return ret;
		}
	}
	return 0;
}

/* Hardware Control Functions */
static int hm0360_hw_reset(const struct device *dev)
{
	const struct hm0360_config *cfg = dev->config;
	int ret;

	/* Configure GPIOs */
	ret = gpio_pin_configure_dt(&cfg->resetn_gpio, GPIO_OUTPUT_ACTIVE);
	ret |= gpio_pin_configure_dt(&cfg->power_gpio, GPIO_OUTPUT_INACTIVE);
	ret |= gpio_pin_configure_dt(&cfg->xsleepn_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret) {
		LOG_ERR("GPIO configuration failed");
		return ret;
	}

	/* Reset sequence */
	k_usleep(100);
	gpio_pin_set_dt(&cfg->power_gpio, 1);
	gpio_pin_set_dt(&cfg->resetn_gpio, 0);
	k_usleep(400);
	gpio_pin_set_dt(&cfg->xsleepn_gpio, 0);
	k_usleep(39);

	return 0;
}

static int hm0360_software_reset(const struct device *dev)
{
	const uint8_t reset_val = 0;
	int ret = hm0360_write_reg(dev, HM0360_SOFTWARE_RESET_REGISTER, reset_val);

	if (ret) {
		LOG_ERR("Soft-reset failed: %d", ret);
		return ret;
	}

	k_msleep(1);
	return 0;
}

static int hm0360_validate_chipid(const struct device *dev)
{
	uint16_t chipid = 0;
	uint8_t val;
	int ret;

	ret = hm0360_read_reg(dev, HM0360_CHIP_ID_L_REGISTER, &val);
	if (ret)
		return ret;
	chipid = val;

	ret = hm0360_read_reg(dev, HM0360_CHIP_ID_H_REGISTER, &val);
	if (ret)
		return ret;
	chipid |= val << 8;

	if (chipid != HM0360_CHIP_ID_VAL) {
		LOG_ERR("Invalid Chip-ID: 0x%x", chipid);
		return -ENOTSUP;
	}

	return 0;
}

/* Video API Implementation */
static int hm0360_set_fmt(const struct device *dev, enum video_endpoint_id ep,
						 struct video_format *fmt)
{
	struct hm0360_data *drv_data = dev->data;
	const struct hm0360_config *cfg = dev->config;
	bool format_found = false;
	int ret;

	/* Skip if format hasn't changed */
	if (!memcmp(&drv_data->fmt, fmt, sizeof(*fmt))) {
		return 0;
	}

	/* Validate format */
	for (size_t i = 0; fmts[i].pixelformat; i++) {
		if (fmt->pixelformat == fmts[i].pixelformat &&
			fmt->width == fmts[i].width_min &&
			fmt->height == fmts[i].height_min) {
			format_found = true;
			break;
		}
	}

	if (!format_found) {
		LOG_ERR("Unsupported format/resolution");
		return -ENOTSUP;
	}

	/* Find and apply resolution config */
	for (size_t i = 0; i < ARRAY_SIZE(resolution_configs); i++) {
		const struct hm0360_resolution_config *res = &resolution_configs[i];

		if (fmt->width == res->width &&
			fmt->height == res->height &&
			cfg->fps == res->fps) {
			ret = hm0360_write_regs(dev, res->params, res->size_params);
			if (ret)
				return ret;

			drv_data->fmt = *fmt;
			LOG_DBG("Format set: %dx%d, pixfmt: 0x%x",
					fmt->width, fmt->height, fmt->pixelformat);
			return 0;
		}
	}

	return -ENOTSUP;
}

static int hm0360_get_fmt(const struct device *dev, enum video_endpoint_id ep,
		struct video_format *fmt)
{
	struct hm0360_data *drv_data = dev->data;

	*fmt = drv_data->fmt;
	return 0;
}

static int hm0360_stream_start(const struct device *dev)
{
	struct hm0360_data *drv_data = dev->data;
	uint8_t mode;
	int ret;

	switch (drv_data->capture_mode) {
	case CONTINUOUS_CAPTURE:
		mode = HM0360_MODE_I2C_TRIGGER_CONT_STREAMING;
		break;
	case SNAPSHOT_CAPTURE:
		/* Set frame count first */
		ret = hm0360_write_reg(dev, HM0360_PMU_CFG_7, drv_data->numframes);
		if (ret)
			return ret;
		mode = HM0360_MODE_I2C_TRIGGER_SNAPSHOT_N_FRAMES;
		break;
	default:
		return -ENOTSUP;
	}

	return hm0360_write_reg(dev, HM0360_MODE_SELECT_REGISTER, mode);
}

static int hm0360_stream_stop(const struct device *dev)
{
	const uint8_t mode = HM0360_MODE_I2C_TRIGGER_SLEEP;

	return hm0360_write_reg(dev, HM0360_MODE_SELECT_REGISTER, mode);
}

static int hm0360_get_caps(const struct device *dev, enum video_endpoint_id ep,
		struct video_caps *caps)
{
	caps->format_caps = fmts;
	return 0;
}

static int hm0360_set_ctrl(const struct device *dev, unsigned int cid, void *value)
{
	struct hm0360_data *drv_data = dev->data;

	switch (cid) {
	case VIDEO_HM0360_CID_SNAPSHOT_CAPTURE:
		drv_data->capture_mode = SNAPSHOT_CAPTURE;
		drv_data->numframes = *(uint8_t *)value;
		LOG_DBG("Snapshot mode, frames: %d", drv_data->numframes);
		return 0;
	case VIDEO_HM0360_CID_CONTINUOUS_CAPTURE:
		drv_data->capture_mode = CONTINUOUS_CAPTURE;
		drv_data->numframes = 0;
		LOG_DBG("Continuous mode");
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct video_driver_api hm0360_driver_api = {
	.set_format = hm0360_set_fmt,
	.get_format = hm0360_get_fmt,
	.get_caps = hm0360_get_caps,
	.stream_start = hm0360_stream_start,
	.stream_stop = hm0360_stream_stop,
	.set_ctrl = hm0360_set_ctrl,
};

static int hm0360_init(const struct device *dev)
{
	int ret;

	/* Initialize pinctrl if enabled */
	if (IS_ENABLED(CONFIG_PINCTRL)) {
		const struct pinctrl_dev_config *pcfg;

		PINCTRL_DT_INST_DEFINE(0);
		pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0);
		if (pcfg) {
			ret = pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
			if (ret) {
				LOG_ERR("Pinctrl init failed: %d", ret);
				return ret;
			}
		}
	}

	/* Hardware reset */
	ret = hm0360_hw_reset(dev);
	if (ret) {
		LOG_ERR("HW reset failed: %d", ret);
		return ret;
	}

	/* Validate chip ID */
	ret = hm0360_validate_chipid(dev);
	if (ret) {
		return ret;
	}

	/* Software reset */
	ret = hm0360_software_reset(dev);
	if (ret) {
		return ret;
	}

	/* Load initial configuration */
	ret = hm0360_write_regs(dev, hm0360_init_config,
			ARRAY_SIZE(hm0360_init_config));
	if (ret) {
		LOG_ERR("Init config failed: %d", ret);
		return ret;
	}

	k_msleep(1);

	ret = hm0360_stream_stop(dev);
	if (ret)
		return ret;
	/* Set default format */
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_BGGR8,
		.width = 640,
		.height = 480,
		.pitch = 640
	};

	return hm0360_set_fmt(dev, VIDEO_EP_OUT, &fmt);
}

/* Device configuration */
static const struct hm0360_config hm0360_config_0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	.resetn_gpio = GPIO_DT_SPEC_INST_GET_OR(0, reset_gpios, {0}),
#endif
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
	.power_gpio = GPIO_DT_SPEC_INST_GET_OR(0, power_gpios, {0}),
#endif
#if DT_INST_NODE_HAS_PROP(0, xsleep_gpios)
	.xsleepn_gpio = GPIO_DT_SPEC_INST_GET_OR(0, xsleep_gpios, {0}),
#endif
	.fps = DT_INST_PROP_OR(0, fps, 60),
};

static struct hm0360_data hm0360_data_0 = {
	.capture_mode = CONTINUOUS_CAPTURE,
	.numframes = 1,
};

DEVICE_DT_INST_DEFINE(0, hm0360_init, NULL, &hm0360_data_0, &hm0360_config_0,
		POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY, &hm0360_driver_api);
