#include <linux/module.h>
#include "aw_breath_algorithm.h"

static const uint8_t gamma_brightness[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 10, 12, 14, 16, 18, 20, 22,
	24, 26, 29, 32, 35, 38, 41, 44,
	47, 50, 53, 57, 61, 65, 69, 73,
	77, 81, 85, 89, 94, 99, 104, 109,
	114, 119, 124, 129, 134, 140, 146, 152,
	158, 164, 170, 176, 182, 188, 195, 202,
	209, 216, 223, 230, 237, 244, 251, 255,
};

static const uint8_t gamma_steps = sizeof(gamma_brightness)/sizeof(uint8_t);

/*
*Perform a better visual LED breathing effect,
*we recommend using a gamma corrected value
*to set the LED intensity.
*This results in a reduced number of steps for
*the LED intensity setting, but causes the change
*in intensity to appear more linear to the human eye.
*/
uint8_t algorithm_get_correction(ALGO_DATA_STRUCT *p_algo_data,int index)
{
	uint16_t start_idx = 0;
	int32_t end_idx = 0;

	if (p_algo_data->cur_frame == 0)
		start_idx = p_algo_data->data_start[index];
	else if ((p_algo_data->total_frames-1) == p_algo_data->cur_frame)
		start_idx = p_algo_data->data_end[index];
	else if (p_algo_data->data_end[index] >= p_algo_data->data_start[index]) {
		/* get the start index in gamma array */
		while (start_idx < gamma_steps) {
			if (gamma_brightness[start_idx] >= p_algo_data->data_start[index])
				break;

			start_idx++;
		}

		if (start_idx >= gamma_steps)
			start_idx = gamma_steps - 1;

		/* get the end index in gamma array */
		end_idx = gamma_steps - 1;
		while (end_idx >= 0) {
			if (gamma_brightness[end_idx] <= p_algo_data->data_end[index])
				break;

			end_idx--;
		}

		if (end_idx < 0)
			end_idx = 0;

		/* get current index */
		start_idx += (end_idx-start_idx) *
					 p_algo_data->cur_frame /
					 (p_algo_data->total_frames-1);
		/* get start index */
		start_idx = gamma_brightness[start_idx];
	} else {
		/* get the start index in gamma array */
		while (start_idx < gamma_steps) {
			if (gamma_brightness[start_idx] >= p_algo_data->data_end[index])
				break;

			start_idx++;
		}

		if (start_idx >= gamma_steps)
			start_idx = gamma_steps - 1;

		/* get the end index in gamma array */
		end_idx = gamma_steps - 1;
		while (end_idx >= 0) {
			if (gamma_brightness[end_idx] <= p_algo_data->data_start[index])
				break;

			end_idx--;
		}

		if (end_idx < 0)
			end_idx = 0;

		/* get currrent index */
		end_idx -= (end_idx-start_idx)*p_algo_data->cur_frame/(p_algo_data->total_frames - 1);
		/* get start index */
		start_idx = gamma_brightness[end_idx];
	}
	//AW_ERR("MTC_LOG: start_idx=%d end_idx=%d\n", start_idx, end_idx);
	return start_idx;
}
//EXPORT_SYMBOL(algorithm_get_correction);

GetBrightnessFuncPtr aw210xx_get_breath_brightness_algo_func(BREATH_ALGO_ID algo_id)
{
	GetBrightnessFuncPtr get_brightness_func_ptr = NULL;

	switch (algo_id) {
	case BREATH_ALGO_GAMMA_CORRECTION:
		get_brightness_func_ptr = algorithm_get_correction;
		break;

	default:
		break;
	}

	return get_brightness_func_ptr;
}
EXPORT_SYMBOL(aw210xx_get_breath_brightness_algo_func);
MODULE_DESCRIPTION("AW210XX LED breath algorithm");
MODULE_LICENSE("GPL v2");

