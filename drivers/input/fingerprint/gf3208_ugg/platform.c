/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 * Copyright (C) 2018 XiaoMi, Inc.
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

static int vreg_setup(struct gf_dev *gf_dev, const char *name,
	bool enable)
{
	size_t i;
	int rc;
	struct regulator *vreg;
	struct device *dev = &gf_dev->spi->dev;

	for (i = 0; i < ARRAY_SIZE(gf_dev->vreg); i++) {
		const char *n = vreg_conf[i].name;

		if (!strncmp(n, name, strlen(n)))
			goto found;
	}

	dev_err(dev, "Regulator %s not found\n", name);

	return -EINVAL;

found:
	vreg = gf_dev->vreg[i];
	if (enable) {
		if (!vreg) {
			vreg = regulator_get(dev, name);
			if (IS_ERR(vreg)) {
				dev_err(dev, "Unable to get %s\n", name);
				return PTR_ERR(vreg);
			}
		}

		if (regulator_count_voltages(vreg) > 0) {
			rc = regulator_set_voltage(vreg, vreg_conf[i].vmin,
					vreg_conf[i].vmax);
			if (rc)
				dev_err(dev,
					"Unable to set voltage on %s, %d\n",
					name, rc);
		}

		rc = regulator_set_load(vreg, vreg_conf[i].ua_load);
		if (rc < 0)
			dev_err(dev, "Unable to set current on %s, %d\n",
					name, rc);

		rc = regulator_enable(vreg);
		if (rc) {
			dev_err(dev, "error enabling %s: %d\n", name, rc);
			regulator_put(vreg);
			vreg = NULL;
		}
		gf_dev->vreg[i] = vreg;
	} else {
		if (vreg) {
			if (regulator_is_enabled(vreg)) {
				regulator_disable(vreg);
				dev_dbg(dev, "disabled %s\n", name);
			}
			regulator_put(vreg);
			gf_dev->vreg[i] = NULL;
		}
		rc = 0;
	}

	return rc;
}

int gf_parse_dts(struct gf_dev* gf_dev)
{
	int rc = 0;

#ifndef CONFIG_MACH_XIAOMI_UTER
	gf_dev->pwr_gpio = of_get_named_gpio(gf_dev->spi->dev.of_node, "goodix,gpio_ldo", 0);
	if (!gpio_is_valid(gf_dev->pwr_gpio)) {
		pr_info("gpio ldo is invalid\n");
		return -1;
	} else {
		pr_debug("gf:ldo gpio:%d\n", gf_dev->pwr_gpio);
		rc = gpio_request(gf_dev->pwr_gpio, "goodix_pwr");
		if (rc) {
			dev_err(&gf_dev->spi->dev, "Failed to request PWR GPIO. rc = %d\n", rc);
			return -1;
		}
		gpio_direction_output(gf_dev->pwr_gpio, 1);

	}
#endif
#ifndef CONFIG_MACH_XIAOMI_UTER
	rc = vreg_setup(gf_dev, "vcc_spi", true);
	if (rc)
		goto exit;

	rc = vreg_setup(gf_dev, "vdd_io", true);
	if (rc)
		goto exit_1;
#endif

	rc = vreg_setup(gf_dev, "vdd_ana", true);
	if (rc)
#ifndef CONFIG_MACH_XIAOMI_UTER
		goto exit_2;
#else
		return rc;
#endif

	msleep(11);
 	printk("gf3208 msleep 11ms\n");

	gf_dev->reset_gpio = of_get_named_gpio(gf_dev->spi->dev.of_node, "goodix,gpio_reset", 0);
	if (!gpio_is_valid(gf_dev->reset_gpio)) {
		pr_info("RESET GPIO is invalid.\n");
		return -1;
	} else {
		pr_debug("gf:reset_gpio:%d\n", gf_dev->reset_gpio);
#if 1
		rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
		if (rc) {
			dev_err(&gf_dev->spi->dev, "Failed to request RESET GPIO. rc = %d\n", rc);
			return -1;
		}
		gpio_direction_output(gf_dev->reset_gpio, 1);
		gpio_free(gf_dev->reset_gpio);
#endif
	}
	gf_dev->irq_gpio = of_get_named_gpio(gf_dev->spi->dev.of_node, "goodix,gpio_irq", 0);
	if (!gpio_is_valid(gf_dev->irq_gpio)) {
		pr_info("IRQ GPIO is invalid.\n");
		return -1;
	} else {
		pr_info("gf:irq_gpio:%d\n", gf_dev->irq_gpio);
#if 1
		rc = gpio_request(gf_dev->irq_gpio, "goodix_irq");
		if (rc) {
			dev_err(&gf_dev->spi->dev, "Failed to request IRQ GPIO. rc = %d\n", rc);

		}
		gpio_direction_input(gf_dev->irq_gpio);
		gpio_free(gf_dev->irq_gpio);
#endif
	}

	return 0;

#ifndef CONFIG_MACH_XIAOMI_UTER
	(void)vreg_setup(gf_dev, "vdd_ana", false);
exit_2:
	(void)vreg_setup(gf_dev, "vdd_io", false);
exit_1:
	(void)vreg_setup(gf_dev, "vcc_spi", false);
exit:
	return rc;
#endif
}

void gf_cleanup(struct gf_dev	* gf_dev)
{
	pr_info("[info] %s\n", __func__);
	if (gpio_is_valid(gf_dev->irq_gpio)) {
		gpio_free(gf_dev->irq_gpio);
		pr_info("remove irq_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_free(gf_dev->reset_gpio);
		pr_info("remove reset_gpio success\n");
	}
	(void)vreg_setup(gf_dev, "vdd_ana", false);
	(void)vreg_setup(gf_dev, "vdd_io", false);
	(void)vreg_setup(gf_dev, "vcc_spi", false);
}

int gf_power_on(struct gf_dev* gf_dev)
{
	int rc = 0;

	msleep(10);
	pr_info("---- power on ok ----\n");

	return rc;
}

int gf_power_off(struct gf_dev* gf_dev)
{
	int rc = 0;

	pr_info("---- power off ----\n");
	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	int rc = 0;

	if (gf_dev == NULL) {
		pr_info("Input Device is NULL.\n");
		return -1;
	}

	if (gpio_is_valid(gf_dev->reset_gpio)) {
		rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
		if (rc) {
			dev_err(&gf_dev->spi->dev, "Failed to request RESET GPIO. rc = %d\n", rc);

		}
		gpio_direction_output(gf_dev->reset_gpio, 1);

		gpio_direction_output(gf_dev->reset_gpio, 1);
		gpio_set_value(gf_dev->reset_gpio, 0);
		mdelay(20);
		gpio_set_value(gf_dev->reset_gpio, 1);
		mdelay(delay_ms);
		gpio_free(gf_dev->reset_gpio);
	} else {
		dev_err(&gf_dev->spi->dev, "reset gpio not valid when reset delay time is %d\n", delay_ms);
		return -1;
	}

	return rc;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if (gf_dev == NULL) {
		pr_info("Input Device is NULL.\n");
		return -1;
	} else {
		if (gpio_is_valid(gf_dev->irq_gpio))
			return gpio_to_irq(gf_dev->irq_gpio);
		else
			return -1;
	}
}

