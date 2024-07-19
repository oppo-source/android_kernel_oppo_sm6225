#ifndef __AW_BREATH_ALGORITHM_H__
#define __AW_BREATH_ALGORITHM_H__

//#include "stdint.h"
//#include "string.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#define AW_DEBUG 							(1)
#define AW210XX_I2C_NAME "aw210xx_led"
#if AW_DEBUG
#define AW_LOG(fmt, args...)	pr_info("[%s] %s %d: " fmt, AW210XX_I2C_NAME, \
		__func__, __LINE__, ##args)
#else
#define AW_LOG(fmt, args...)
#endif
#define AW_ERR(fmt, args...)	pr_err("[%s] %s %d: " fmt, AW210XX_I2C_NAME, \
		__func__, __LINE__, ##args)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
typedef enum{
	BREATH_ALGO_NONE,
	BREATH_ALGO_GAMMA_CORRECTION,
	BREATH_ALGO_LINEAR_CORRECTION,
	BREATH_ALGO_MAX,
} BREATH_ALGO_ID;

typedef struct{
	uint16_t total_frames;
	uint16_t cur_frame;
	uint16_t data_start[3];
	uint16_t data_end[3];
} ALGO_DATA_STRUCT;

typedef uint8_t (*GetBrightnessFuncPtr)(ALGO_DATA_STRUCT *, int );
extern
GetBrightnessFuncPtr aw210xx_get_breath_brightness_algo_func(BREATH_ALGO_ID algo_id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* __AW_BREATH_ALGORITHM_H__ */
