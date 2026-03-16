#ifndef __OCP2130_BIAS__
#define __OCP2130_BIAS__

struct ocp2130 {
	struct device *dev;
	struct i2c_client *client;
	struct mutex i2c_rw_lock;
	int enp_gpio;
	int enn_gpio;
	//int vddio_gpio;
};

int ocp2130_enable(void);
int ocp2130_disable(void);
//void panel_vddio_enable(int enable);

#endif