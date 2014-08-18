/* arch/arm/mach-msm/htc_acoustic_alsa.c
 *
 * Copyright (C) 2012 HTC Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/gpio.h>
#include <mach/htc_acoustic_alsa.h>

#define D(fmt, args...) printk(KERN_INFO "[AUD] htc-acoustic: "fmt, ##args)
#define E(fmt, args...) printk(KERN_ERR "[AUD] htc-acoustic: "fmt, ##args)

#define GPIO_AUD_RT_1V8_EN 120
static bool power_on = false;

static struct amp_power_ops amp_power = {

};

static int __init amp_power_init(void)
{
	int ret = 0;
	D("%s", __func__);
	htc_amp_power_register_ops(&amp_power);
	return ret;
}

static void __exit amp_power_exit(void)
{
}

arch_initcall(amp_power_init);
module_exit(amp_power_exit);
