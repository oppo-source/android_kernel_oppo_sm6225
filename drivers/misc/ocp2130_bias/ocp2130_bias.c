#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ocp2130_bias.h>

#define OCP2130_Positive_Output	0X00
#define OCP2130_Negative_Output	0X01
#define OCP2130_Set_voltage	0x14
#define OCP2130_Set_vsp_vsn_enable	0x03
#define OCP2130_Set_value	0x03
#define OCP2130_Mtp_Reg		0xFF
#define OCP2130_Mtp_save	0x80
struct i2c_client *gclient;

/*
 * static int __ocp2130_read_reg(struct ocp2130 *ocp, u8 reg, u8 *data)
 * {
 * 	s32 ret;
 *
 * 	ret = i2c_smbus_read_byte_data(ocp->client, reg);
 * 	if (ret < 0) {
 * 		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
 * 		pm_relax(ocp->dev);
 * 		return ret;
 * 	}
 *
 * 	*data = (u8)ret;
 *
 * 	return 0;
 * 	}*/
static int __ocp2130_write_reg(struct ocp2130 *ocp, int reg, u8 val)
{
	s32 ret;
	ret = i2c_smbus_write_byte_data(ocp->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
				val, reg, ret);
		pm_relax(ocp->dev);
		return ret;
	}
	return 0;
}
static int ocp2130_write_reg(struct ocp2130 *ocp, u8 reg, u8 data)
{
	int ret;
	mutex_lock(&ocp->i2c_rw_lock);
	ret = __ocp2130_write_reg(ocp, reg, data);
	mutex_unlock(&ocp->i2c_rw_lock);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
	return ret;
}
/*
 * static int ocp2130_read_reg(struct ocp2130 *ocp, u8 reg, u8 *data)
 * {
 * 	int ret;
 *
 * 	mutex_lock(&ocp->i2c_rw_lock);
 * 	ret = __ocp2130_read_reg(ocp, reg, data);
 * 	mutex_unlock(&ocp->i2c_rw_lock);
 *
 * 	return ret;
 *
 * }*/


static void ocp2130_parse_dt(struct device *dev, struct ocp2130 *ocp)
{
	struct device_node *np = dev->of_node;
	int ret;

	ocp->enp_gpio = of_get_named_gpio(np, "lcm-enp-gpio", 0);
	pr_info("enp_gpio: %d\n", ocp->enp_gpio);

	ocp->enn_gpio = of_get_named_gpio(np, "lcm-enn-gpio", 0);
	pr_info("enn_gpio: %d\n", ocp->enn_gpio);

//	ocp->vddio_gpio = of_get_named_gpio(np, "lcm-vddio-gpio", 0);
//	pr_info("vddio_gpio: %d\n", ocp->vddio_gpio);

	if (gpio_is_valid(ocp->enp_gpio)) {
		ret = gpio_request(ocp->enp_gpio, "lcm-enp-gpio");
		if (ret < 0) {
			pr_err("failed to request lcm-enp-gpio\n");
		}
		pr_info("enp_gpio is valid!\n");
	}
	if (gpio_is_valid(ocp->enn_gpio)) {
		ret = gpio_request(ocp->enn_gpio, "lcm-enn-gpio");
		if (ret < 0) {
			pr_err("failed to request lcm-enn-gpio\n");
		}
		pr_info("enn_gpio is valid!\n");
	}
/*
    if (gpio_is_valid(ocp->vddio_gpio)) {
		ret = gpio_request(ocp->vddio_gpio, "lcm-vddio-gpio");
		if (ret < 0) {
			pr_err("failed to request lcm-vddio-gpio\n");
		}
		pr_info("lcm-vddio-gpio is valid!\n");
	}
*/
}

int ocp2130_enable(void)
{
	struct ocp2130 *ocp = i2c_get_clientdata(gclient);

	pr_info("[drm]%s: entry\n", __func__);
	if (ocp == NULL) {
		pr_err("[BIAS]%s ocp is null\n", __func__);
		return -ENOMEM;
	}
	gpio_set_value(ocp->enp_gpio, 1);
	ocp2130_write_reg(ocp, OCP2130_Positive_Output, OCP2130_Set_voltage);
	ocp2130_write_reg(ocp, OCP2130_Negative_Output, OCP2130_Set_voltage);
	ocp2130_write_reg(ocp, OCP2130_Set_vsp_vsn_enable, OCP2130_Set_value);

	mdelay(5);
	gpio_set_value(ocp->enn_gpio, 1);
	return 0;
}
EXPORT_SYMBOL_GPL(ocp2130_enable);

int ocp2130_disable(void)
{
	struct ocp2130 *ocp = i2c_get_clientdata(gclient);

	pr_info("[drm]%s: entry\n", __func__);
	if (ocp == NULL) {
		pr_err("[BIAS]%s ocp is null\n", __func__);
		return -ENOMEM;
	}
	gpio_set_value(ocp->enn_gpio, 0);
	mdelay(5);
	gpio_set_value(ocp->enp_gpio, 0);
	mdelay(1);
	return 0;
}
EXPORT_SYMBOL_GPL(ocp2130_disable);

/*
void panel_vddio_enable(int enable)
{
	struct ocp2130 *ocp = i2c_get_clientdata(gclient);

	pr_info("[drm]%s: entry\n", __func__);
	if (enable) {
		gpio_set_value(ocp->vddio_gpio, 0);
		pr_info("[drm]%s = 0\n", __func__);
	} else {
		gpio_set_value(ocp->vddio_gpio, 1);
		pr_info("[drm]%s = 1\n", __func__);
	}
}
EXPORT_SYMBOL_GPL(panel_vddio_enable);
*/

static int ocp2130_bias_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ocp2130 *ocp;
	pr_info("Enter ocp2130_bias_probe\n");
	ocp = devm_kzalloc(&client->dev, sizeof(struct ocp2130), GFP_KERNEL);
	if (!ocp) {
		pr_err("Out of memory\n");
		return -ENOMEM;
	}
	ocp->dev = &client->dev;
	ocp->client = client;
	gclient = client;
	i2c_set_clientdata(client, ocp);
	ocp2130_parse_dt(&client->dev, ocp);
	mutex_init(&ocp->i2c_rw_lock);
	//ocp2130_write_reg(ocp, OCP2130_Positive_Output, OCP2130_Set_voltage);
	//ocp2130_write_reg(ocp, OCP2130_Negative_Output, OCP2130_Set_voltage);
	//ocp2130_write_reg(ocp, OCP2130_Set_vsp_vsn_enable, OCP2130_Set_value);
	//ocp2130_write_reg(ocp, OCP2130_Mtp_Reg, OCP2130_Mtp_save);
	return 0;
}
static int ocp2130_bias_remove(struct i2c_client *client)
{
	struct ocp2130 *ocp = i2c_get_clientdata(client);
	mutex_destroy(&ocp->i2c_rw_lock);
	pr_info("Enter ocp2130_bias_remove\n");
	return 0;
}
/*****************************************************************************
 * * i2c driver configuration
 * *****************************************************************************/
static const struct i2c_device_id ocp2130_bias_id[] = {
	{"ocp2130_bias", 0},
	{},
};
static const struct of_device_id ocp2130_bias_match_table[] = {
	{.compatible = "qcom,ocp2130_bias"},
	{},
};
MODULE_DEVICE_TABLE(of, ocp2130_bias_match_table);
MODULE_DEVICE_TABLE(i2c, ocp2130_bias_id);
static struct i2c_driver ocp2130_bias_driver = {
	.driver = {
		.name = "ocp2130_bias",
		.owner 	= THIS_MODULE,
		.of_match_table = ocp2130_bias_match_table,
	},
	.id_table = ocp2130_bias_id,
	.probe = ocp2130_bias_probe,
	.remove = ocp2130_bias_remove,
};

static int __init ocp2130_init(void)
{
	int ret = 0;

	pr_info("%s Entry\n", __func__);

	ret = i2c_add_driver(&ocp2130_bias_driver);
	if (ret != 0)
		pr_err("OCP2130 driver init failed!");

	pr_info("%s Complete\n", __func__);

	return ret;
}

static void __exit ocp2130_exit(void) 
{
	i2c_del_driver(&ocp2130_bias_driver);
}

module_init(ocp2130_init);
module_exit(ocp2130_exit);

MODULE_AUTHOR("OCP");
MODULE_DESCRIPTION("OCP2130 Bias IC Driver");
MODULE_LICENSE("GPL v2");
