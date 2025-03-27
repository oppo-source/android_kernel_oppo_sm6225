#include "wl2866d-regulator.h"

#define WL2866D_REG_ID			0x00
#define WL2866D_REG_ILIMIT		0x01
#define WL2866D_REG_RDIS		0x02
#define WL2866D_REG_DVDD1_VOUT	0x03
#define WL2866D_REG_DVDD_SEQ	0x0A
#define WL2866D_REG_AVDD_SEQ	0x0B
#define WL2866D_REG_ENABLE		0x0E
#define WL2866D_REG_SEQCR		0x0F

#define WL2866D_LDO_DISABLE_ALL 0x00

#define LDO_VSET_REG(offset) ((offset) + WL2866D_REG_DVDD1_VOUT)

#define WL2866D_VSET_BASE_DVDD    600
#define WL2866D_VSET_BASE_AVDD    1200
#define WL2866D_VSET_STEP_UV_DVDD 6000
#define WL2866D_VSET_STEP_UV_AVDD 12500

#define SGM38121_VSET_BASE_DVDD    504
#define SGM38121_VSET_BASE_AVDD    1384
#define SGM38121_VSET_STEP_UV_DVDD 8000
#define SGM38121_VSET_STEP_UV_AVDD 8000

#define WL2866D_MAX_LDO 4

#define WL2866D_NAME "qualcomm,wl2866d"
#define WL2866D_DRIVER_VERSION	"V1.0.0"

/*second supplier*/
#define WL2866D_REGULATOR_CHIP_ID   0x00
#define SGM38121_REGULATOR_CHIP_ID  0x80
#define REGULATOR_LDO_RDIS          0x0F

enum devices
{
	WL2866D = 1,
	SGM38121
} ;


#define DEVICE_NAME "WL2866D:"

int ldo_devices = 0;
static int log_enable = 0;

#define LDO_LOGE(reg, message, ...) \
		pr_err("%s: %s %s [%d]" message, DEVICE_NAME, (reg)->rdesc.name,__FUNCTION__, __LINE__ ,##__VA_ARGS__)
#define LDO_LOGI(reg, message, ...) \
		pr_info("%s: %s %s [%d]" message, DEVICE_NAME, (reg)->rdesc.name,__FUNCTION__, __LINE__ , ##__VA_ARGS__)
#define LDO_LOGV(reg, message, ...) \
		if(log_enable){ \
			pr_info("%s: %s %s [%d]: "message,DEVICE_NAME,(reg)->rdesc.name,__FUNCTION__, __LINE__ ,##__VA_ARGS__); \
		}
#define LOGV(message, ...) \
		if(log_enable){ \
			pr_info("%s: %s [%d]: "message,DEVICE_NAME, __FUNCTION__, __LINE__ ,##__VA_ARGS__); \
		}


#define LOGI(message, ...) \
	 	pr_info("%s: %s [%d] " message,DEVICE_NAME , __FUNCTION__, __LINE__ ,##__VA_ARGS__)

#define LOGE(message, ...) \
	 	pr_err("%s: %s [%d] " message,DEVICE_NAME , __FUNCTION__, __LINE__,##__VA_ARGS__)

struct wl2866d_chip_data{
	struct device *dev;
	struct regmap    *regmap;
	int    ldo_status;
	struct mutex lock;
};

struct wl2866d_regulator{
	struct wl2866d_chip_data * chip_data;
	struct regulator_desc rdesc;
	struct regulator_dev  *rdev;
	struct device_node    *of_node;
	u16         offset;
	int         min_dropout_uv;
	int         iout_ua;
	int			cur_vol_min;
	int 		cur_vol_max;
	int 		cur_enable_status;
};

struct regulator_data {
	char *name;
	char *supply_name;
	int default_mv;
	int  min_dropout_uv;
	int iout_ua;
};

static struct regulator_data reg_data[] = {
	/* name,  parent,   headroom */
	{ "wl2866d-dvdd1", "", 1200, 225000, 650000},
	{ "wl2866d-dvdd2", "", 1200, 225000, 650000},
	{ "wl2866d-avdd1", "", 2800, 200000, 650000},
	{ "wl2866d-avdd2", "", 2800, 200000, 650000},
};

static struct regmap *g_regmap = NULL;
static const struct regmap_config wl2866d_regmap_config = {
        .reg_bits = 8,
        .val_bits = 8,
};
/*common functions*/
static int wl2866d_read(struct wl2866d_regulator *fan_reg, uint32_t reg, uint32_t *val, int count)
{
	int rc = 0;
	struct regmap    *regmap = fan_reg->chip_data->regmap;
	if(regmap != NULL){
		rc = regmap_bulk_read(regmap, reg, val, count);
		if (rc < 0)
			LDO_LOGE(fan_reg,"regmap failed to read 0x%04x\n", reg);

	}
	LDO_LOGV(fan_reg,"read 0x%02x value 0x%04x with count %d\n",  reg, *val,count);
	return rc;
}

static int wl2866d_write(struct wl2866d_regulator *fan_reg, uint32_t reg, uint32_t*val, int count)
{
	int rc = 0;
	struct regmap *regmap = fan_reg->chip_data->regmap;
	LDO_LOGV(fan_reg,"Writing 0x%02x to 0x%04x with count %d\n", *val, reg, count);
	if(regmap != NULL){
		rc = regmap_bulk_write(regmap, reg, val, count);
		if (rc < 0)
			LDO_LOGE(fan_reg,"regmap failed to write 0x%04x\n", reg);
	}
	return rc;
}

static int wl2866d_masked_write(struct wl2866d_regulator *fan_reg, uint32_t reg, u8 mask, uint32_t val)
{
	int rc = 0;
	u32 oldValue = 0, writeValue = val;

	LDO_LOGV(fan_reg,"Writing 0x%02x to 0x%04x with mask 0x%02x\n", val, reg, mask);

	rc = wl2866d_read(fan_reg , reg , &oldValue , 1);

	if(rc < 0){
		LDO_LOGE(fan_reg,"failed read");

	}else{
		writeValue = (~mask) &  oldValue;
		writeValue = val | writeValue;

	}
	rc = wl2866d_write(fan_reg,reg,&writeValue,1);
	if (rc < 0)
		LDO_LOGE(fan_reg,"failed to write 0x%02x to 0x%04x with mask 0x%02x\n", val, reg, mask);
	return rc;
}

static ssize_t wl2866d_reg_debug_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned char reg_val = 0;
	int i = 0,length = 0,rc = 0;

	for( i = 0; i <= WL2866D_REG_SEQCR; i++){
		rc = regmap_bulk_read(g_regmap, i, &reg_val, 1);
		if (rc < 0){
			LOGE("read 0x%x failed",i);
		}else{
			LOGV("read addr 0x%x value 0x%x",i,reg_val);
			length += snprintf(buf+length, 20, "addr 0x%x data %d\n",i,reg_val);
		}
	};
	return length;

}

static ssize_t wl2866d_reg_debug_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int ret = 0;
	unsigned char reg_val = 0;
	ret = kstrtou8(buf, 0, &reg_val);
	if (ret < 0)
		return ret;
	log_enable = reg_val;
	return count;
}

static DEVICE_ATTR(reg_debug, 0664, wl2866d_reg_debug_show, wl2866d_reg_debug_store);

static struct attribute *wl2866d_attributes[] = {
	&dev_attr_reg_debug.attr,
	NULL
};

static struct attribute_group wl2866d_attribute_group = {
	.attrs = wl2866d_attributes
};


static int _wl2866d_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	u32 vset;
	int rc = 0;
	int uv = 0;

	rc = wl2866d_read(fan_reg, LDO_VSET_REG(fan_reg->offset),&vset, 1);
	if (rc < 0) {
		LDO_LOGE(fan_reg,
			"failed to read regulator voltage rc = %d\n", rc);
		return rc;
	}

	if (vset == 0) {
		uv = reg_data[fan_reg->offset].default_mv;
	} else {
        switch (ldo_devices) {
	        case WL2866D:
		    {
		        LDO_LOGV(fan_reg, "voltage read [%x]\n", vset);
		        if (fan_reg->offset == 0 || fan_reg->offset == 1)
		        	uv = WL2866D_VSET_BASE_DVDD *1000 + vset * WL2866D_VSET_STEP_UV_DVDD;
		        else
		        	uv = WL2866D_VSET_BASE_AVDD *1000 + vset * WL2866D_VSET_STEP_UV_AVDD;
		    }
		    break;
	        case SGM38121:
		    {
		        LDO_LOGV(fan_reg, "voltage read [%x]\n", vset);
		        if (fan_reg->offset == 0 || fan_reg->offset == 1)
		        	uv = SGM38121_VSET_BASE_DVDD *1000 + vset * SGM38121_VSET_STEP_UV_DVDD;
		        else
		        	uv = SGM38121_VSET_BASE_AVDD *1000 + vset * SGM38121_VSET_STEP_UV_AVDD;
		    }
		    break;
	        default:
		        LDO_LOGE(fan_reg,"Not supported device \n");
		}
	}
	LDO_LOGV(fan_reg,"read reg 0x%02x uv %d \n",  vset , uv);
	return uv;
}

static int wl2866d_write_voltage(struct wl2866d_regulator* fan_reg, int min_uv,
	int max_uv)
{
	int rc = 0, mv;
	u32 vset;

	mv = DIV_ROUND_UP(min_uv, 1000);
	if (mv*1000 > max_uv) {
		LDO_LOGE(fan_reg, "requestd voltage above maximum limit\n");
		return -EINVAL;
	}
    switch (ldo_devices) {
        case WL2866D:
            LDO_LOGV(fan_reg, "min_uv read [%x]\n", min_uv);
            if (fan_reg->offset == 0 || fan_reg->offset == 1)
            	vset =  DIV_ROUND_UP(min_uv - WL2866D_VSET_BASE_DVDD *1000, WL2866D_VSET_STEP_UV_DVDD);
            else
            	vset =  DIV_ROUND_UP(min_uv - WL2866D_VSET_BASE_AVDD *1000, WL2866D_VSET_STEP_UV_AVDD);
            break;
        case SGM38121:
            LDO_LOGV(fan_reg, "min_uv read [%x]\n", min_uv);
            if (fan_reg->offset == 0 || fan_reg->offset == 1)
            	vset =  DIV_ROUND_UP(min_uv - SGM38121_VSET_BASE_DVDD *1000, SGM38121_VSET_STEP_UV_DVDD);
            else
            	vset =  DIV_ROUND_UP(min_uv - SGM38121_VSET_BASE_AVDD *1000, SGM38121_VSET_STEP_UV_AVDD);
            break;
        default:
            LDO_LOGE(fan_reg,"Not supported device \n");
    }

	rc = wl2866d_write(fan_reg, LDO_VSET_REG(fan_reg->offset),&vset, 1);
	if (rc < 0) {
		LDO_LOGE(fan_reg, "failed to write voltage rc = %d\n", rc);
		return rc;
	}
	LDO_LOGV(fan_reg,"write vol %d:%d reg 0x%x \n",  min_uv , max_uv,vset);
	return 0;
}


static int wl2866d_regulator_enable(struct regulator_dev *rdev)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	int rc,reg,uv;

	wl2866d_write_voltage(fan_reg,fan_reg->cur_vol_min,fan_reg->cur_vol_max);
	mutex_lock(&fan_reg->chip_data->lock);
	rc = wl2866d_masked_write(fan_reg,WL2866D_REG_ENABLE,
		(1u<<fan_reg->offset), (1u<<fan_reg->offset));
	mutex_unlock(&fan_reg->chip_data->lock);
	if (rc < 0) {
		LDO_LOGE(fan_reg, "failed to enable regulator rc=%d\n", rc);
		return rc;
	}
	rc = wl2866d_read(fan_reg,WL2866D_REG_ENABLE, &reg, 1);
	if(rc < 0)
		LDO_LOGE(fan_reg,"faild read reg,rc= %d",rc);
	uv = _wl2866d_regulator_get_voltage(rdev);
	fan_reg->cur_enable_status = 1;
	fan_reg->chip_data->ldo_status = fan_reg->chip_data->ldo_status | (1u<<fan_reg->offset);

	LDO_LOGI(fan_reg, "regulator enable reg 0x%x uv %d\n",reg,uv);
	return 0;
}

static int wl2866d_regulator_disable(struct regulator_dev *rdev)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	int rc = 0;
	mutex_lock(&fan_reg->chip_data->lock);
	rc = wl2866d_masked_write(fan_reg,WL2866D_REG_ENABLE,
		1u<<fan_reg->offset, 0);
	mutex_unlock(&fan_reg->chip_data->lock);
	if (rc < 0) {
		LDO_LOGE(fan_reg,
			"failed to disable regulator rc=%d\n", rc);
		return rc;
	}
	fan_reg->cur_enable_status = 0;
	fan_reg->chip_data->ldo_status = fan_reg->chip_data->ldo_status & ~(1u<<fan_reg->offset);

	LDO_LOGI(fan_reg, "regulator disabled\n");
	return 0;
}

static int wl2866d_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	return fan_reg->cur_enable_status;
}

static int wl2866d_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	return fan_reg->cur_vol_min;
}
static int wl2866d_regulator_set_voltage(struct regulator_dev *rdev,
	int min_uv, int max_uv, unsigned int* selector)
{
	struct wl2866d_regulator *fan_reg = rdev_get_drvdata(rdev);
	int rc = 0;

	fan_reg->cur_vol_min = min_uv;
	fan_reg->cur_vol_max = max_uv;

	return rc;
}

static struct regulator_ops wl2866d_regulator_ops = {
	.enable = wl2866d_regulator_enable,
	.disable = wl2866d_regulator_disable,
	.is_enabled = wl2866d_regulator_is_enabled,
	.set_voltage = wl2866d_regulator_set_voltage,
	.get_voltage = wl2866d_regulator_get_voltage,
};

static int wl2866d_register_ldo(struct wl2866d_regulator *wl2866d_reg,
	const char *name)
{
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;

	struct device_node *reg_node = wl2866d_reg->of_node;
	struct device *dev = wl2866d_reg->chip_data->dev;
	int rc, i, init_voltage;


	/* try to find ldo pre-defined in the regulator table */
	for (i = 0; i< WL2866D_MAX_LDO; i++) {
		if (!strcmp(reg_data[i].name, name))
			break;
	}

	if ( i == WL2866D_MAX_LDO) {
		LOGE("Invalid regulator name %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u16(reg_node, "offset", &wl2866d_reg->offset);
	if (rc < 0) {
		LOGE("%s:failed to get regulator offset rc = %d\n", name, rc);
		return rc;
	}

	//assign default value defined in code.
	wl2866d_reg->min_dropout_uv = reg_data[i].min_dropout_uv;
	of_property_read_u32(reg_node, "min-dropout-voltage",
		&wl2866d_reg->min_dropout_uv);

	wl2866d_reg->iout_ua = reg_data[i].iout_ua;
	of_property_read_u32(reg_node, "iout_ua",
		&wl2866d_reg->iout_ua);

	init_voltage = -EINVAL;
	of_property_read_u32(reg_node, "init-voltage", &init_voltage);


	init_data = of_get_regulator_init_data(dev, reg_node, &wl2866d_reg->rdesc);
	if (init_data == NULL) {
		LOGE("%s: failed to get regulator data\n", name);
		return -ENODATA;
	}


	if (!init_data->constraints.name) {
		LOGE("%s: regulator name missing\n", name);
		return -EINVAL;
	}

	/* configure the initial voltage for the regulator */
	if (init_voltage > 0) {
		rc = wl2866d_write_voltage(wl2866d_reg, init_voltage,
			init_data->constraints.max_uV);
		if (rc < 0)
			LOGE("%s:failed to set initial voltage rc = %d\n", name, rc);
	}

	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS
		| REGULATOR_CHANGE_VOLTAGE;

	reg_config.dev = dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = wl2866d_reg;
	reg_config.of_node = reg_node;

	wl2866d_reg->rdesc.owner = THIS_MODULE;
	wl2866d_reg->rdesc.type = REGULATOR_VOLTAGE;
	wl2866d_reg->rdesc.ops = &wl2866d_regulator_ops;

	if(wl2866d_reg->chip_data->regmap == NULL)
		wl2866d_reg->rdesc.supply_name = reg_data[i].supply_name;

	wl2866d_reg->rdesc.name = init_data->constraints.name;
	wl2866d_reg->rdesc.n_voltages = 1;

	LOGI("try to register ldo %s\n", name);
	wl2866d_reg->rdev = devm_regulator_register(dev, &wl2866d_reg->rdesc,
		&reg_config);
	if (IS_ERR(wl2866d_reg->rdev)) {
		rc = PTR_ERR(wl2866d_reg->rdev);
		LOGE("%s: failed to register regulator rc =%d\n",
		wl2866d_reg->rdesc.name, rc);
		return rc;
	}

	LOGI("%s regulator register done\n", name);
	return 0;
}

static int wl2866d_parse_regulator(struct wl2866d_chip_data * chip_data)
{
	int rc = 0;
	const char *name;
	struct device_node *child;
	struct device * dev = chip_data->dev;
	struct wl2866d_regulator *wl2866d_reg;

	/* parse each regulator */
	for_each_available_child_of_node(dev->of_node, child) {
		wl2866d_reg = devm_kzalloc(dev, sizeof(*wl2866d_reg), GFP_KERNEL);
		if (!wl2866d_reg)
			return -ENOMEM;

		wl2866d_reg->chip_data = chip_data;
		wl2866d_reg->of_node = child;
		wl2866d_reg->cur_vol_min = 0;
		wl2866d_reg->cur_vol_max = 0;
		wl2866d_reg->cur_enable_status = 0;

		rc = of_property_read_string(child, "regulator-name", &name);
		if (rc)
			continue;

		rc = wl2866d_register_ldo(wl2866d_reg, name);
		if (rc <0 ) {
			LOGE("failed to register regulator %s rc = %d\n", name, rc);
			return rc;
		}
	}
	return 0;
}

static int wl2866d_regulator_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int rc = 0;
	unsigned int val = 0;
	struct wl2866d_chip_data * chip_data = NULL;
	struct regmap *regmap;

	LOGI("++");
	regmap = devm_regmap_init_i2c(client, &wl2866d_regmap_config);
	if (IS_ERR(regmap)) {
		LOGE("wl2866d failed to allocate regmap\n");
		return PTR_ERR(regmap);
	}

	g_regmap = regmap;

	chip_data =  kzalloc(sizeof(struct wl2866d_chip_data), GFP_KERNEL);
	if (!(chip_data)) {
		LOGE("failed alloc chip_data");
		return -ENOMEM;
	}

	chip_data->dev = &client->dev;
	i2c_set_clientdata(client, chip_data);
	dev_set_drvdata(chip_data->dev, chip_data);
	//TODO: Retry 3 Times Read Reg 0x00
	if(g_regmap != NULL){
		rc = regmap_read(g_regmap, WL2866D_REG_ID, &val);
		if (rc < 0) {
            LOGE("wl2866d failed to get PID rc = %d",rc);
		}
		else{
			switch (val) {
	        case WL2866D_REGULATOR_CHIP_ID:
			    LOGE("wl2866d get Product ID: [%02x]\n", val);
	        	ldo_devices = WL2866D;
				regmap_write(g_regmap, WL2866D_REG_RDIS, 0x8f);
				break;
	        case SGM38121_REGULATOR_CHIP_ID:
			    LOGE("sgm38121 get Product ID: [%02x]\n", val);
	        	ldo_devices = SGM38121;
				break;
	        default:
	        	LOGE("Not supported device \n");
			}
		}

		rc = regmap_write(g_regmap, WL2866D_REG_ENABLE, WL2866D_LDO_DISABLE_ALL);
		if (rc < 0) {
			LOGE("%s disable ldo failed rc = %d", (ldo_devices == WL2866D)?"wl2866d":"sgm238121", rc);
		}
	}

	chip_data->regmap = g_regmap;
	mutex_init(&chip_data->lock);
	rc = wl2866d_parse_regulator(chip_data);
	if (rc < 0) {
		LOGE("wl2866d failed to parse device tree rc=%d\n", rc);
	}
	rc = sysfs_create_group(&client->dev.kobj,
					   &wl2866d_attribute_group);

	if (rc < 0) {
		LOGE("error creating sysfs attr files\n");
	}

	LOGI("--- rc = %d",rc);
	return rc;
}

static int wl2866d_remove(struct i2c_client *client)
{
	struct wl2866d_chip_data *chip = i2c_get_clientdata(client);

	devm_kfree(chip->dev, chip);
	chip = NULL;
	return 0;
}

static const struct i2c_device_id wl2866d_id_table[] = {
	{WL2866D_NAME, 0},
	{} /* NULL terminated */
};

MODULE_DEVICE_TABLE(i2c, wl2866d_id_table);


#ifdef CONFIG_OF
static const struct of_device_id wl2866d_i2c_of_match_table[] = {
		{ .compatible = WL2866D_NAME },
		{},
};
MODULE_DEVICE_TABLE(of, wl2866d_i2c_of_match_table);
#endif

static struct i2c_driver wl2866d_driver = {
	.driver = {
		.name = WL2866D_NAME,
		.of_match_table = of_match_ptr(wl2866d_i2c_of_match_table),
		},
	.probe = wl2866d_regulator_probe,
	.remove = wl2866d_remove,
	.id_table = wl2866d_id_table,
};

static int __init wl2866d_i2c_init(void)
{
	LOGI("wl2866d driver version %s\n", WL2866D_DRIVER_VERSION);
	return i2c_add_driver(&wl2866d_driver);
}
subsys_initcall(wl2866d_i2c_init);

static void __exit wl2866d_i2c_exit(void)
{
	i2c_del_driver(&wl2866d_driver);
}
module_exit(wl2866d_i2c_exit);

MODULE_DESCRIPTION("WL2866D driver");
MODULE_AUTHOR("awinic,lnc.");
MODULE_LICENSE("GPL");
