// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFD core driver for Ricoh RN5T618 PMIC
 * Note: Manufacturer is now Nisshinbo Micro Devices Inc.
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2016 Toradex AG
 */

#include <common.h>
#include <driver.h>
#include <errno.h>
#include <i2c/i2c.h>
#include <init.h>
#include <of.h>
#include <linux/regmap.h>
#include <reset_source.h>
#include <restart.h>
#include <mfd/rn5t568.h>

struct rn5t568 {
	struct restart_handler restart;
	struct regmap *regmap;
};

static void rn5t568_restart(struct restart_handler *rst)
{
	struct rn5t568 *rn5t568 = container_of(rst, struct rn5t568, restart);

	regmap_write(rn5t568->regmap, RN5T568_SLPCNT, RN5T568_SLPCNT_SWPPWROFF);
}

static int rn5t568_reset_reason_detect(struct device *dev,
				       struct regmap *regmap)
{
	enum reset_src_type type;
	unsigned int reg;
	int ret;

	ret = regmap_read(regmap, RN5T568_PONHIS, &reg);
	if (ret)
		return ret;

	dev_dbg(dev, "Power-on history: %x\n", reg);

	if (reg == 0) {
		dev_info(dev, "No power-on reason available\n");
		return 0;
	}

	if (reg & (RN5T568_PONHIS_ON_EXTINPON | RN5T568_PONHIS_ON_PWRONPON)) {
		type = RESET_POR;
	} else if (!(reg & RN5T568_PONHIS_ON_REPWRPON)) {
		return -EINVAL;
	} else {
		ret = regmap_read(regmap, RN5T568_POFFHIS, &reg);
		if (ret)
			return ret;

		dev_dbg(dev, "Power-off history: %x\n", reg);

		if (reg & RN5T568_POFFHIS_PWRONPOFF)
			type = RESET_POR;
		else if (reg & RN5T568_POFFHIS_TSHUTPOFF)
			type = RESET_THERM;
		else if (reg & RN5T568_POFFHIS_VINDETPOFF)
			type = RESET_BROWNOUT;
		else if (reg & RN5T568_POFFHIS_IODETPOFF)
			type = RESET_UKWN;
		else if (reg & RN5T568_POFFHIS_CPUPOFF)
			type = RESET_RST;
		else if (reg & RN5T568_POFFHIS_WDGPOFF)
			type = RESET_WDG;
		else if (reg & RN5T568_POFFHIS_DCLIMPOFF)
			type = RESET_BROWNOUT;
		else if (reg & RN5T568_POFFHIS_N_OEPOFF)
			type = RESET_EXT;
		else
			return -EINVAL;
	}

	reset_source_set_device(dev, type, 200);
	return 0;
}

static const struct regmap_config rn5t568_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= RN5T568_MAX_REG,
};

static int __init rn5t568_i2c_probe(struct device *dev)
{
	struct rn5t568 *pmic_instance;
	unsigned char reg[2];
	int ret;

	pmic_instance = xzalloc(sizeof(struct rn5t568));
	pmic_instance->regmap = regmap_init_i2c(to_i2c_client(dev), &rn5t568_regmap_config);
	if (IS_ERR(pmic_instance->regmap))
		return PTR_ERR(pmic_instance->regmap);

	ret = regmap_register_cdev(pmic_instance->regmap, NULL);
	if (ret)
		return ret;

	ret = regmap_bulk_read(pmic_instance->regmap, RN5T568_LSIVER, &reg, 2);
	if (ret) {
		dev_err(dev, "Failed to read PMIC version via I2C\n");
		return ret;
	}

	dev_info(dev, "Found NMD RN5T568 LSI %x, OTP: %x\n", reg[0], reg[1]);

	/* Settings used to trigger software reset and by a watchdog trigger */
	regmap_write(pmic_instance->regmap, RN5T568_REPCNT, RN5T568_REPCNT_OFF_RESETO_16MS |
		     RN5T568_REPCNT_OFF_REPWRTIM_1000MS | RN5T568_REPCNT_OFF_REPWRON);

	pmic_instance->restart.of_node = dev->of_node;
	pmic_instance->restart.name = "RN5T568";
	pmic_instance->restart.restart = &rn5t568_restart;
	restart_handler_register(&pmic_instance->restart);
	dev_dbg(dev, "RN5t: Restart handler with priority %d registered\n", pmic_instance->restart.priority);

	ret = rn5t568_reset_reason_detect(dev, pmic_instance->regmap);
	if (ret)
		dev_warn(dev, "Failed to query reset reason\n");

	return of_platform_populate(dev->of_node, NULL, dev);
}

static __maybe_unused const struct of_device_id rn5t568_of_match[] = {
	{ .compatible = "ricoh,rn5t568", .data = NULL, },
	{ }
};
MODULE_DEVICE_TABLE(of, rn5t568_of_match);

static struct driver rn5t568_i2c_driver = {
	.name		= "rn5t568-i2c",
	.probe		= rn5t568_i2c_probe,
	.of_compatible	= DRV_OF_COMPAT(rn5t568_of_match),
};

coredevice_i2c_driver(rn5t568_i2c_driver);
