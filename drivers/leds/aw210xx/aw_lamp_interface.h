#ifndef _AW_LAMP_INTERFACE_H_
#define _AW_LAMP_INTERFACE_H_

//#include "stm32h7xx.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "aw_breath_algorithm.h"

typedef struct{
	uint8_t r;
	uint8_t g;
	uint8_t b;
} AW_COLOR_STRUCT;

typedef struct {
	uint32_t frame_factor;
	uint32_t time[6];
	uint32_t repeat_nums;
	uint8_t color_nums;
    AW_COLOR_STRUCT *rgb_color_list;
	AW_COLOR_STRUCT *fadeh;
	AW_COLOR_STRUCT *fadel;
} AW_MULTI_BREATH_DATA_STRUCT;

typedef struct{
	GetBrightnessFuncPtr getBrightnessfunc;
	uint16_t total_frames;
	uint16_t cur_frame;
	ALGO_DATA_STRUCT *p_algo_data;
	AW_COLOR_STRUCT *p_color_1;
	AW_COLOR_STRUCT *p_color_2;
} AW_COLORFUL_INTERFACE_STRUCT;

extern void aw210xx_set_colorful_rgb_data(uint8_t rgb_idx, uint8_t *dim_reg,
	AW_COLORFUL_INTERFACE_STRUCT *p_colorful_interface);
extern void aw210xx_set_rgb_brightness(unsigned char rgb_idx,
	unsigned char *fade_reg, unsigned char *brightness);

extern unsigned char aw_get_real_dim(unsigned char led_dim);
#endif
