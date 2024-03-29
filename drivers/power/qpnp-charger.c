/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt)	"[BATT][CHG] %s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/radix-tree.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/power_supply.h>
#include <linux/bitops.h>
#include <linux/qpnp/qpnp-charger.h>
#include <linux/qpnp/qpnp-bms.h>
#include <linux/ratelimit.h>
#include <mach/cable_detect.h>
#include <mach/devices_cmdline.h>
#include <mach/devices_dtb.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of_batterydata.h>
#include <linux/qpnp-revid.h>
#include <linux/android_alarm.h>
#include <mach/htc_gauge.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <mach/socinfo.h>
#ifdef CONFIG_HTC_BATT_8960
#include "mach/htc_battery_cell.h"
#endif

#ifdef CONFIG_FORCE_FAST_CHARGE
#include <linux/fastchg.h>
#endif

#ifdef pr_debug
#undef pr_debug
#endif
#define pr_debug(fmt, args...) do { \
		if (flag_enable_bms_charger_log) \
			printk(KERN_INFO pr_fmt(fmt), ## args); \
	} while (0)

/* Interrupt offsets */
#define INT_RT_STS(base)			(base + 0x10)
#define INT_SET_TYPE(base)			(base + 0x11)
#define INT_POLARITY_HIGH(base)			(base + 0x12)
#define INT_POLARITY_LOW(base)			(base + 0x13)
#define INT_LATCHED_CLR(base)			(base + 0x14)
#define INT_EN_SET(base)			(base + 0x15)
#define INT_EN_CLR(base)			(base + 0x16)
#define INT_LATCHED_STS(base)			(base + 0x18)
#define INT_PENDING_STS(base)			(base + 0x19)
#define INT_MID_SEL(base)			(base + 0x1A)
#define INT_PRIORITY(base)			(base + 0x1B)

/* Peripheral register offsets */
#define CHGR_FSM_STATE				0xE7
#define CHGR_CHG_OPTION				0x08
#define CHGR_ATC_STATUS				0x0A
#define CHGR_VBAT_STATUS			0x0B
#define CHGR_IBAT_BMS				0x0C
#define CHGR_IBAT_STS				0x0D
#define CHGR_VDD_MAX				0x40
#define CHGR_VDD_SAFE				0x41
#define CHGR_VDD_MAX_STEP			0x42
#define CHGR_IBAT_MAX				0x44
#define CHGR_IBAT_SAFE				0x45
#define CHGR_VIN_MIN				0x47
#define CHGR_VIN_MIN_STEP			0x48
#define CHGR_CHG_CTRL				0x49
#define CHGR_CHG_FAILED				0x4A
#define CHGR_ATC_CTRL				0x4B
#define CHGR_ATC_FAILED				0x4C
#define CHGR_VBAT_TRKL				0x50
#define CHGR_VBAT_WEAK				0x52
#define CHGR_IBAT_ATC_A				0x54
#define CHGR_IBAT_ATC_B				0x55
#define CHGR_IBAT_TERM_CHGR			0x5B
#define CHGR_IBAT_TERM_BMS			0x5C
#define CHGR_VBAT_DET				0x5D
#define CHGR_TTRKL_MAX_EN			0x5E
#define CHGR_TTRKL_MAX				0x5F
#define CHGR_TCHG_MAX_EN			0x60
#define CHGR_TCHG_MAX				0x61
#define CHGR_CHG_WDOG_TIME			0x62
#define CHGR_CHG_WDOG_DLY			0x63
#define CHGR_CHG_WDOG_PET			0x64
#define CHGR_CHG_WDOG_EN			0x65
#define CHGR_IR_DROP_COMPEN			0x67
#define CHGR_VDD_MAX_COMPEN			0x68
#define CHGR_I_MAX_REG			0x44
#define CHGR_I_TRIM_UNBLOCK		0xD0
#define CHGR_I_TRIM_REG			0xF1
#define CHGR_USB_USB_SUSP			0x47
#define CHGR_USB_USB_OTG_CTL			0x48
#define CHGR_USB_ENUM_T_STOP			0x4E
#define CHGR_CHG_TEMP_THRESH			0x66
#define CHGR_BAT_IF_PRES_STATUS			0x08
#define CHGR_STATUS				0x09
#define CHGR_BAT_IF_VCP				0x42
#define CHGR_BAT_IF_BATFET_CTRL1		0x90
#define CHGR_BAT_IF_SPARE			0xDF
#define CHGR_MISC_BOOT_DONE			0x42
#define CHGR_BUCK_PSTG_CTRL			0x73
#define CHGR_BUCK_COMPARATOR_OVRIDE_1		0xEB
#define CHGR_BUCK_COMPARATOR_OVRIDE_3		0xED
#define CHGR_BUCK_BCK_VBAT_REG_MODE		0x74
#define MISC_REVISION2				0x01
#define USB_OVP_CTL				0x42
#define USB_CHG_GONE_REV_BST			0xED
#define BUCK_VCHG_OV				0x77
#define BUCK_TEST_SMBC_MODES			0xE6
#define SEC_ACCESS				0xD0
#define BAT_IF_VREF_BAT_THM_CTRL		0x4A
#define BAT_IF_BPD_CTRL				0x48
#define BOOST_VSET				0x41
#define BOOST_ENABLE_CONTROL			0x46
#define COMP_OVR1				0xEA
#define BAT_IF_BTC_CTRL				0x49
#define USB_OCP_THR				0x52
#define USB_OCP_CLR				0x53
#define BAT_IF_TEMP_STATUS			0x09

#define REG_OFFSET_PERP_SUBTYPE			0x05

/* SMBB battery interface registers offset  */
#define BAT_IF_BAT_TEMP_STATUS		0x09
#define BAT_IF_BAT_FET_STATUS		0x0B
#define BAT_IF_VREF_BAT_THM_CTRL	0x4A
#define BAT_IF_BPD_CTRL				0x48
#define BAT_IF_BTC_CTRL				0x49

/* SMBB USB chgpth registers offset  */
#define CHGPTH_USB_CHG_PTH_STS		0x09
#define CHGPTH_CHG_GONE_INT			0x0A
#define CHGPTH_AICL_STS				0x0C
#define CHGPTH_AICL_I_AUTO			0x0D
#define CHGPTH_AICL_I_TGT			0x0E
#define CHGPTH_INT_SET_TYPE			0x11
#define CHGPTH_INT_POLARITY_HIGH		0x12
#define CHGPTH_USB_OVP_CTL			0x42
#define CHGPTH_IUSB_MAX				0x44
#define CHGPTH_USB_SUSP				0x47
#define CHGPTH_USB_OTG_CTL			0x48
#define CHGPTH_ENUM_TIMER_STOP		0x4E
#define CHGPTH_ENUM_TIMER			0x4F

/* SMBB DC chgpth registers offset  */
#define CHGPTH_DC_CHG_PTH_STS		0x0A

/* SMBB peripheral subtype values */
#define SMBB_CHGR_SUBTYPE			0x01
#define SMBB_BUCK_SUBTYPE			0x02
#define SMBB_BAT_IF_SUBTYPE			0x03
#define SMBB_USB_CHGPTH_SUBTYPE			0x04
#define SMBB_DC_CHGPTH_SUBTYPE			0x05
#define SMBB_BOOST_SUBTYPE			0x06
#define SMBB_MISC_SUBTYPE			0x07

/* SMBB peripheral subtype values */
#define SMBBP_CHGR_SUBTYPE			0x31
#define SMBBP_BUCK_SUBTYPE			0x32
#define SMBBP_BAT_IF_SUBTYPE			0x33
#define SMBBP_USB_CHGPTH_SUBTYPE		0x34
#define SMBBP_BOOST_SUBTYPE			0x36
#define SMBBP_MISC_SUBTYPE			0x37

/* SMBCL peripheral subtype values */
#define SMBCL_CHGR_SUBTYPE			0x41
#define SMBCL_BUCK_SUBTYPE			0x42
#define SMBCL_BAT_IF_SUBTYPE			0x43
#define SMBCL_USB_CHGPTH_SUBTYPE		0x44
#define SMBCL_MISC_SUBTYPE			0x47

#define QPNP_CHARGER_DEV_NAME	"qcom,qpnp-charger"

#define REG_PMIC_HWREV					0x103

/* Status bits and masks */
#define CHGR_BOOT_DONE			BIT(7)
#define CHGR_CHG_EN			BIT(7)
#define CHGR_ON_BAT_FORCE_BIT		BIT(0)
#define USB_VALID_DEB_20MS		0x03
#define BUCK_VBAT_REG_NODE_SEL_BIT	BIT(0)
#define VREF_BATT_THERM_FORCE_ON	0xC0
#define BAT_IF_BPD_CTRL_SEL		0x03
#define VREF_BAT_THM_ENABLED_FSM	0x80
#define REV_BST_DETECTED		BIT(0)
#define BAT_THM_EN			BIT(1)
#define BAT_ID_EN			BIT(0)
#define BOOST_PWR_EN			BIT(7)
#define OCP_CLR_BIT			BIT(7)
#define OCP_THR_MASK			0x03
#define OCP_THR_900_MA			0x02
#define OCP_THR_500_MA			0x01
#define OCP_THR_200_MA			0x00
#define BAT_IF_BTC_CTRL_SEL		0x80
#define BAT_IF_BAT_TEMP_HOT		BIT(6)
#define BAT_IF_BAT_TEMP_OK		BIT(7)

/* Interrupt definitions */
/* smbb_chg_interrupts */
#define CHG_DONE_IRQ			BIT(7)
#define CHG_FAILED_IRQ			BIT(6)
#define FAST_CHG_ON_IRQ			BIT(5)
#define TRKL_CHG_ON_IRQ			BIT(4)
#define STATE_CHANGE_ON_IR		BIT(3)
#define CHGWDDOG_IRQ			BIT(2)
#define VBAT_DET_HI_IRQ			BIT(1)
#define VBAT_DET_LOW_IRQ		BIT(0)

/* smbb_buck_interrupts */
#define VDD_LOOP_IRQ			BIT(6)
#define IBAT_LOOP_IRQ			BIT(5)
#define ICHG_LOOP_IRQ			BIT(4)
#define VCHG_LOOP_IRQ			BIT(3)
#define OVERTEMP_IRQ			BIT(2)
#define VREF_OV_IRQ			BIT(1)
#define VBAT_OV_IRQ			BIT(0)

/* smbb_bat_if_interrupts */
#define PSI_IRQ				BIT(4)
#define VCP_ON_IRQ			BIT(3)
#define BAT_FET_ON_IRQ			BIT(2)
#define BAT_TEMP_OK_IRQ			BIT(1)
#define BATT_PRES_IRQ			BIT(0)

/* smbb_usb_interrupts */
#define CHG_GONE_IRQ			BIT(2)
#define USBIN_VALID_IRQ			BIT(1)
#define COARSE_DET_USB_IRQ		BIT(0)

/* smbb_dc_interrupts */
#define DCIN_VALID_IRQ			BIT(1)
#define COARSE_DET_DC_IRQ		BIT(0)

/* smbb_boost_interrupts */
#define LIMIT_ERROR_IRQ			BIT(1)
#define BOOST_PWR_OK_IRQ		BIT(0)

/* smbb_misc_interrupts */
#define TFTWDOG_IRQ			BIT(0)

/* SMBB types */
#define SMBB				BIT(1)
#define SMBBP				BIT(2)
#define SMBCL				BIT(3)

/* Workaround flags */
#define CHG_FLAGS_VCP_WA		BIT(0)
#define BOOST_FLASH_WA			BIT(1)
#define POWER_STAGE_WA			BIT(2)

/* USB current limit*/
#define USB_MA_0       (0)
#define USB_MA_2       (2)
#define USB_MA_100     (100)
#define USB_MA_200     (200)
#define USB_MA_300     (300)
#define USB_MA_400     (400)
#define USB_MA_500     (500)
#define USB_MA_900     (900)
#define USB_MA_1100    (1100)
#define USB_MA_1500	(1500)
#define USB_MA_1600	(1600)

/* Regulate VIN_MIN */
#define VIN_MIN_4400_MV	4400

/* Safety timer 8 & 8.5 values (mins)*/
#define SAFETY_TIME_MAX_LIMIT		510
#define SAFETY_TIME_8HR_TWICE		480

// TODO: need USB owner to provide a common API
#define DWC3_DCP	2

#ifdef CONFIG_ARCH_MSM8226
#define TEST_EN_SMBC_LOOP		0xE5
#define IBAT_REGULATION_DISABLE		BIT(2)
#endif

struct qpnp_chg_irq {
	int		irq;
	unsigned long		disabled;
};

struct qpnp_chg_regulator {
	struct regulator_desc			rdesc;
	struct regulator_dev			*rdev;
};

/**
 * struct qpnp_chg_chip - device information
 * @dev:			device pointer to access the parent
 * @spmi:			spmi pointer to access spmi information
 * @chgr_base:			charger peripheral base address
 * @buck_base:			buck  peripheral base address
 * @bat_if_base:		battery interface  peripheral base address
 * @usb_chgpth_base:		USB charge path peripheral base address
 * @dc_chgpth_base:		DC charge path peripheral base address
 * @boost_base:			boost peripheral base address
 * @misc_base:			misc peripheral base address
 * @freq_base:			freq peripheral base address
 * @bat_is_cool:		indicates that battery is cool
 * @bat_is_warm:		indicates that battery is warm
 * @chg_done:			indicates that charging is completed
 * @usb_present:		present status of usb
 * @dc_present:			present status of dc
 * @batt_present:		present status of battery
 * @use_default_batt_values:	flag to report default battery properties
 * @btc_disabled		Flag to disable btc (disables hot and cold irqs)
 * @max_voltage_mv:		the max volts the batt should be charged up to
 * @min_voltage_mv:		min battery voltage before turning the FET on
 * @batt_weak_voltage_mv:	Weak battery voltage threshold
 * @max_bat_chg_current:	maximum battery charge current in mA
 * @warm_bat_chg_ma:	warm battery maximum charge current in mA
 * @cool_bat_chg_ma:	cool battery maximum charge current in mA
 * @warm_bat_mv:		warm temperature battery target voltage
 * @cool_bat_mv:		cool temperature battery target voltage
 * @resume_delta_mv:		voltage delta at which battery resumes charging
 * @term_current:		the charging based term current
 * @safe_current:		battery safety current setting
 * @maxinput_usb_ma:		Maximum Input current USB
 * @maxinput_dc_ma:		Maximum Input current DC
 * @hot_batt_p			Hot battery threshold setting
 * @cold_batt_p			Cold battery threshold setting
 * @warm_bat_decidegc		Warm battery temperature in degree Celsius
 * @cool_bat_decidegc		Cool battery temperature in degree Celsius
 * @revision:			PMIC revision
 * @type:			SMBB type
 * @tchg_mins			maximum allowed software initiated charge time
 * @thermal_levels		amount of thermal mitigation levels
 * @thermal_mitigation		thermal mitigation level values
 * @therm_lvl_sel		thermal mitigation level selection
 * @dc_psy			power supply to export information to userspace
 * @usb_psy			power supply to export information to userspace
 * @bms_psy			power supply to export information to userspace
 * @batt_psy:			power supply to export information to userspace
 * @flags:			flags to activate specific workarounds
 *				throughout the driver
 *
 */
struct qpnp_chg_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				buck_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				dc_chgpth_base;
	u16				boost_base;
	u16				misc_base;
	u16				freq_base;
	struct qpnp_chg_irq		usbin_valid;
	struct qpnp_chg_irq		usb_ocp;
	struct qpnp_chg_irq		dcin_valid;
	struct qpnp_chg_irq		chg_gone;
	struct qpnp_chg_irq		chg_fastchg;
	struct qpnp_chg_irq		chg_trklchg;
	struct qpnp_chg_irq		chg_failed;
	struct qpnp_chg_irq		chg_vbatdet_lo;
	struct qpnp_chg_irq		chg_vbatdet_hi;
	struct qpnp_chg_irq		chgwdog;
	struct qpnp_chg_irq		chg_state_change;
	struct qpnp_chg_irq		chg_is_done;
	struct qpnp_chg_irq		buck_vbat_ov;
	struct qpnp_chg_irq		buck_vreg_ov;
	struct qpnp_chg_irq		buck_overtemp;
	struct qpnp_chg_irq		buck_ichg_loop;
	struct qpnp_chg_irq		buck_ibat_loop;
	struct qpnp_chg_irq		buck_vdd_loop;
	struct qpnp_chg_irq		batt_pres;
	struct qpnp_chg_irq		vchg_loop;
	struct qpnp_chg_irq		batt_temp_ok;
	struct qpnp_chg_irq		batt_fet_on;
	struct qpnp_chg_irq		vcp_on;
	struct qpnp_chg_irq		coarse_usb_det;
	struct qpnp_chg_irq		coarse_dc_det;
	struct qpnp_chg_irq		boost_pwr_ok;
	struct qpnp_chg_irq		limit_error;
	bool				bat_is_cool;
	bool				bat_is_warm;
	bool				chg_done;
	bool				charger_monitor_checked;
	bool				usb_present;
	bool				dc_present;
	bool				batt_present;
	bool				charging_disabled;
	bool				btc_disabled;
	bool				use_default_batt_values;
	bool				duty_cycle_100p;
	bool				enable_qct_adjust_vddmax;
	unsigned int			bpd_detection;
	unsigned int			max_bat_chg_current;
	unsigned int			warm_bat_chg_ma;
	unsigned int			cool_bat_chg_ma;
	unsigned int			safe_voltage_mv;
	unsigned int			max_voltage_mv;
	unsigned int			min_voltage_mv;
	unsigned int			batt_weak_voltage_mv;
	int				prev_usb_max_ma;
	int				set_vddmax_mv;
	int				delta_vddmax_mv;
	int				pre_delta_vddmax_mv;
	unsigned int			warm_bat_mv;
	unsigned int			cool_bat_mv;
	unsigned int			resume_delta_mv;
	int				term_current;
	int				eoc_ibat_thre_ma;
	int				is_embeded_batt;
	int				soc_resume_limit;
	bool				resuming_charging;
	unsigned int			maxinput_usb_ma;
	unsigned int			maxinput_dc_ma;
	unsigned int			hot_batt_p;
	unsigned int			cold_batt_p;
	int				is_pm8921_aicl_enabled;
	int				is_qc20_chg_enabled;
	int				is_aicl_qc20_enable_board_version;
	int				retry_aicl_cnt;
	unsigned int			qc20_ibatmax;
	unsigned int			qc20_ibatsafe;
	int				warm_bat_decidegc;
	int				cool_bat_decidegc;
	int				regulate_vin_min_thr_mv;
	int				lower_vin_min;
	int				fake_battery_soc;
	unsigned int			safe_current;
	unsigned int			revision;
	unsigned int			type;
	unsigned int			tchg_mins;
	unsigned int			thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
	struct power_supply		dc_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*bms_psy;
	struct power_supply		batt_psy;
	uint32_t			flags;
	struct qpnp_adc_tm_btm_param	adc_param;
	struct ext_usb_chg_pm8941	*ext_usb;	/* for ext usb_in charger */
	struct work_struct		adc_measure_work;
	struct work_struct		adc_disable_work;
	struct delayed_work		fix_reverse_boost_check_work;
	struct delayed_work		arb_stop_work;
	struct delayed_work		eoc_work;
	struct work_struct		soc_check_work;
	struct delayed_work		aicl_check_work;
	struct delayed_work     ovpfet_resistance_check_work;
	struct delayed_work		vin_collapse_check_work;
	struct delayed_work		resume_vddmax_configure_work;
	struct delayed_work		update_ovp_uvp_work;
	struct delayed_work		readjust_vddmax_configure_work;
	struct workqueue_struct 	*aicl_check_wq;
	struct qpnp_chg_regulator	otg_vreg;
	struct qpnp_chg_regulator	boost_vreg;
	struct qpnp_chg_regulator	batfet_vreg;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_adc_tm_chip		*adc_tm_dev;
	struct mutex			jeita_configure_lock;
	struct alarm			reduce_power_stage_alarm;
	struct work_struct		reduce_power_stage_work;
	struct wake_lock		vin_collapse_check_wake_lock;
	struct wake_lock		reverse_boost_wa_wake_lock;
	struct wake_lock		set_vbatdet_lock;
	bool				power_stage_workaround_running;
	bool				power_stage_workaround_enable;
	int 				otg_en_gpio;
	int					ext_ovpfet_enable_board_version;
	int					ext_ovpfet_gpio;
	int					ext_ovpfet_rext;
	int					ext_ovpfet_rext_cool;
	int					ext_ovpfet_temp_cool;
};

struct htc_chg_timer {
	unsigned long last_do_jiffies;
	unsigned long total_time_ms;
};
static struct htc_chg_timer aicl_timer, retry_aicl_timer;

/* 1.Disable temp protect. 2.Skip safety timer. 3.keep charging even if full */
static bool flag_keep_charge_on;
/* 1.Disable temp protect. 2.Skip safety timer. */
static bool flag_pa_recharge;
static struct qpnp_chg_chip *the_chip;
static u8 pmic_rev = 0;
#ifdef CONFIG_HTC_BATT_8960
enum htc_power_source_type pwr_src;
#endif /* !CONFIG_HTC_BATT_8960 */

/* latest ov/uv rt state */
static int ovp = false;
static int uvp = false;

/* default AICL target value is 1500ma */
static int aicl_target_ma = USB_MA_1500;

/* static function declaration */
static void update_ovp_uvp_state(int ov, int v, int uv);
static int is_ac_online(void);
static int get_prop_batt_present(struct qpnp_chg_chip *chip);
int pm8941_get_chgr_fsm_state(struct qpnp_chg_chip *chip);
static int qpnp_chg_is_batt_temp_ok(struct qpnp_chg_chip *chip);
static bool vddmax_modify = false;

/* to dump BMS and Charger log*/
static bool flag_enable_bms_charger_log;
#define BATT_LOG_BUF_LEN (1024)
static char batt_log_buf[BATT_LOG_BUF_LEN];
static int eoc_count; /* used in eoc_worker() */
static int eoc_count_by_curr; /* used in eoc_worker() */
static bool is_batt_full = false;
static bool is_batt_full_eoc_stop = false;
static bool is_ac_safety_timeout = false;
static bool is_ac_safety_timeout_twice = false; /* It works if safety_time > 510 */
static bool is_aicl_worker_enabled = false;
static int hsml_target_ma;
static int usb_target_ma;
static int is_vin_min_detected;
static int usb_wall_threshold_ma;
static unsigned int chg_limit_current; /* set from other drivers*/
static int thermal_mitigation;
static void qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip);
#define PM8941_CHG_I_MIN_MA            225
#define QPNP_CHG_VDDMAX_MIN		3400

#define HYSTERISIS_DECIDEGC 20
#define READJUST_VDDMAX_THR_MA		(-50)

static bool is_usb_target_ma_changed_by_qc20 = false;

static int qpnp_chg_vddmax_set(struct qpnp_chg_chip *chip, int voltage);
static void qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip);
static void
qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip);
static int qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current);
int htc_battery_is_support_qc20(void);
int htc_battery_check_cable_type_from_usb(void);

static struct of_device_id qpnp_charger_match_table[] = {
	{ .compatible = QPNP_CHARGER_DEV_NAME, },
	{}
};

enum bpd_type {
	BPD_TYPE_BAT_ID,
	BPD_TYPE_BAT_THM,
	BPD_TYPE_BAT_THM_BAT_ID,
};

static const char * const bpd_label[] = {
	[BPD_TYPE_BAT_ID] = "bpd_id",
	[BPD_TYPE_BAT_THM] = "bpd_thm",
	[BPD_TYPE_BAT_THM_BAT_ID] = "bpd_thm_id",
};

enum btc_type {
	HOT_THD_25_PCT = 25,
	HOT_THD_35_PCT = 35,
	COLD_THD_70_PCT = 70,
	COLD_THD_80_PCT = 80,
};

static u8 btc_value[] = {
	[HOT_THD_25_PCT] = 0x0,
	[HOT_THD_35_PCT] = BIT(0),
	[COLD_THD_70_PCT] = 0x0,
	[COLD_THD_80_PCT] = BIT(1),
};

	static inline int
get_bpd(const char *name)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(bpd_label); i++) {
		if (strcmp(bpd_label[i], name) == 0)
			return i;
	}
	return -EINVAL;
}

static int
qpnp_chg_read(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n", base,
				spmi->sid, rc);
		return rc;
	}
	return 0;
}

static int
qpnp_chg_write(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("write failed base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return rc;
	}

	return 0;
}

static int
qpnp_chg_masked_write(struct qpnp_chg_chip *chip, u16 base,
						u8 mask, u8 val, int count)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg);

	reg &= ~mask;
	reg |= val & mask;

	pr_debug("Writing 0x%x\n", reg);

	rc = qpnp_chg_write(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}

	return 0;
}

static void
qpnp_chg_enable_irq(struct qpnp_chg_irq *irq)
{
	if (__test_and_clear_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		enable_irq(irq->irq);
	}
}

static void
qpnp_chg_disable_irq(struct qpnp_chg_irq *irq)
{
	if (!__test_and_set_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		disable_irq_nosync(irq->irq);
	}
}

#define USB_OTG_EN_BIT	BIT(0)
static int
qpnp_chg_is_otg_en_set(struct qpnp_chg_chip *chip)
{
	u8 usb_otg_en;
	int rc;

	rc = qpnp_chg_read(chip, &usb_otg_en,
				 chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
				 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL, rc);
		return rc;
	}
	pr_debug("usb otg en 0x%x\n", usb_otg_en);

	return (usb_otg_en & USB_OTG_EN_BIT) ? 1 : 0;
}

static int
qpnp_chg_is_boost_en_set(struct qpnp_chg_chip *chip)
{
	u8 boost_en_ctl;
	int rc;

	rc = qpnp_chg_read(chip, &boost_en_ctl,
		chip->boost_base + BOOST_ENABLE_CONTROL, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->boost_base + BOOST_ENABLE_CONTROL, rc);
		return rc;
	}

	pr_debug("boost en 0x%x\n", boost_en_ctl);

	return (boost_en_ctl & BOOST_PWR_EN) ? 1 : 0;
}

static int
qpnp_chg_is_batt_temp_ok(struct qpnp_chg_chip *chip)
{
	u8 batt_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_rt_sts & BAT_TEMP_OK_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_batt_present(struct qpnp_chg_chip *chip)
{
	u8 batt_pres_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_pres_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_pres_rt_sts & BATT_PRES_IRQ) ? 1 : 0;
}

#if !(defined(CONFIG_HTC_BATT_8960))
static int
qpnp_chg_is_batfet_closed(struct qpnp_chg_chip *chip)
{
	u8 batfet_closed_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batfet_closed_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batfet_closed_rt_sts & BAT_FET_ON_IRQ) ? 1 : 0;
}
#endif /* !CONFIG_HTC_BATT_8960 */

#define USB_VALID_BIT	BIT(7)
static int
qpnp_chg_is_usb_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 usbin_valid_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_valid_rt_sts,
				 chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("chgr usb sts 0x%x\n", usbin_valid_rt_sts);

	return (usbin_valid_rt_sts & USB_VALID_BIT) ? 1 : 0;
}

#define USB_ABOVE_OV_BIT	BIT(6)
static int
get_prop_usb_valid_status(struct qpnp_chg_chip *chip, int *ov, int *v, int *uv)
{
	int rc;
	u8 usbin_valid_rt_sts;

	rc = qpnp_chg_read(chip, &usbin_valid_rt_sts,
						chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}

	if (usbin_valid_rt_sts & USB_VALID_BIT)
		*v = true;
	else if (usbin_valid_rt_sts & USB_ABOVE_OV_BIT)
		*ov = true;
	else
		*uv = true;
	pr_debug("chgr usb sts 0x%x\n", usbin_valid_rt_sts);
	return 0;
}

static int
qpnp_chg_is_dc_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 dcin_valid_rt_sts;
	int rc;

	if (!chip->dc_chgpth_base)
		return 0;

	rc = qpnp_chg_read(chip, &dcin_valid_rt_sts,
				 INT_RT_STS(chip->dc_chgpth_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->dc_chgpth_base), rc);
		return rc;
	}

	return (dcin_valid_rt_sts & DCIN_VALID_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_ichg_loop_active(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & ICHG_LOOP_IRQ) ? 1 : 0;
}

#define QPNP_CHG_I_MAX_MIN_100		100
#define QPNP_CHG_I_MAX_MIN_150		150
#define QPNP_CHG_I_MAX_MIN_MA		200
#define QPNP_CHG_I_MAX_MAX_MA		2500
#define QPNP_CHG_I_MAXSTEP_MA		100
static int
qpnp_chg_idcmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 dc = 0;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		dc = 0x00;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		dc = 0x01;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	dc = mA / QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("current=%d setting 0x%x\n", mA, dc);
	rc = qpnp_chg_write(chip, &dc,
		chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);

	return rc;
}

#define QPNP_CHG_I_TRIM_MIN		0
#define QPNP_CHG_I_TRIM_MAX		63
static int
qpnp_chg_iusbtrim_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 usb_reg = 0;
	u8 usb_reg_unlock = 0xA5;

	if (mA < QPNP_CHG_I_TRIM_MIN
			|| mA > QPNP_CHG_I_TRIM_MAX) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	usb_reg = mA;

	rc = qpnp_chg_write(chip, &usb_reg_unlock,
		chip->usb_chgpth_base + CHGR_I_TRIM_UNBLOCK, 1);
	if (rc)
		pr_err("CHGR_I_TRIM_UNBLOCK fail. rc=%d\n", rc);

	rc = qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_TRIM_REG, 1);
	if (rc)
		pr_err("CHGR_I_TRIM_REG fail. rc=%d\n", rc);

	return rc;
}

static int
qpnp_chg_iusbmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 usb_reg = 0, temp = 8;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		usb_reg = 0x00;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		usb_reg = 0x01;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	/* Impose input current limit */
	if (chip->maxinput_usb_ma)
		mA = (chip->maxinput_usb_ma) <= mA ? chip->maxinput_usb_ma : mA;

	usb_reg = mA / QPNP_CHG_I_MAXSTEP_MA;

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x0C, 1);
	}

	pr_debug("current=%d setting 0x%x\n", mA, usb_reg);
	rc = qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		udelay(200);
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x00, 1);
	}

	return rc;
}

#define QPNP_CHG_VINMIN_MIN_MV		4200
#define QPNP_CHG_VINMIN_HIGH_MIN_MV	5600
#define QPNP_CHG_VINMIN_HIGH_MIN_VAL	0x2B
#define QPNP_CHG_VINMIN_MAX_MV		9600
#define QPNP_CHG_VINMIN_STEP_MV		50
#define QPNP_CHG_VINMIN_STEP_HIGH_MV	200
#define QPNP_CHG_VINMIN_MASK		0x3F
#define QPNP_CHG_VINMIN_MIN_VAL	0x10
static int
qpnp_chg_vinmin_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_VINMIN_MIN_MV
			|| voltage > QPNP_CHG_VINMIN_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	if (voltage >= QPNP_CHG_VINMIN_HIGH_MIN_MV) {
		temp = QPNP_CHG_VINMIN_HIGH_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_HIGH_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_HIGH_MV;
	} else {
		temp = QPNP_CHG_VINMIN_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_MV;
	}

	pr_info("%s: voltage=%d setting %02x\n", __func__, voltage, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_VIN_MIN,
			QPNP_CHG_VINMIN_MASK, temp, 1);
}

static int
qpnp_chg_vinmin_get(struct qpnp_chg_chip *chip)
{
	int rc, vin_min_mv;
	u8 vin_min;

	rc = qpnp_chg_read(chip, &vin_min, chip->chgr_base + CHGR_VIN_MIN, 1);
	if (rc) {
		pr_err("failed to read VIN_MIN rc=%d\n", rc);
		return 0;
	}

	if (vin_min == 0)
		vin_min_mv = QPNP_CHG_I_MAX_MIN_100;
	else if (vin_min >= QPNP_CHG_VINMIN_HIGH_MIN_VAL)
		vin_min_mv = QPNP_CHG_VINMIN_HIGH_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_HIGH_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_HIGH_MV;
	else
		vin_min_mv = QPNP_CHG_VINMIN_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_MV;
	pr_debug("vin_min= 0x%02x, ma = %d\n", vin_min, vin_min_mv);

	return vin_min_mv;
}

#define QPNP_CHG_VBATWEAK_MIN_MV	2100
#define QPNP_CHG_VBATWEAK_MAX_MV	3600
#define QPNP_CHG_VBATWEAK_STEP_MV	100
static int
qpnp_chg_vbatweak_set(struct qpnp_chg_chip *chip, int vbatweak_mv)
{
	u8 temp;

	if (vbatweak_mv < QPNP_CHG_VBATWEAK_MIN_MV
			|| vbatweak_mv > QPNP_CHG_VBATWEAK_MAX_MV)
		return -EINVAL;

	temp = (vbatweak_mv - QPNP_CHG_VBATWEAK_MIN_MV)
			/ QPNP_CHG_VBATWEAK_STEP_MV;

	pr_info("voltage=%d setting %02x\n", vbatweak_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_WEAK, 1);
}

static int
qpnp_chg_usb_iusbtrim_get(struct qpnp_chg_chip *chip)
{
	int rc, iusbmax_ma;
	u8 iusbmax;

	rc = qpnp_chg_read(chip, &iusbmax,
		chip->usb_chgpth_base + CHGR_I_TRIM_REG, 1);
	if (rc) {
		pr_err("failed to read IUSB_TRIM rc=%d\n", rc);
		return 0;
	}

	iusbmax_ma = (int)iusbmax;

	pr_debug("iusbtrim = 0x%02x, ma = %d\n", iusbmax, iusbmax_ma);

	return iusbmax_ma;
}

static int
qpnp_chg_usb_iusbmax_get(struct qpnp_chg_chip *chip)
{
	int rc, iusbmax_ma;
	u8 iusbmax;

	rc = qpnp_chg_read(chip, &iusbmax,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	if (rc) {
		pr_err("failed to read IUSB_MAX rc=%d\n", rc);
		return 0;
	}

	if (iusbmax == 0)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_100;
	else if (iusbmax == 0x01)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_150;
	else
		iusbmax_ma = iusbmax * QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("iusbmax = 0x%02x, ma = %d\n", iusbmax, iusbmax_ma);

	return iusbmax_ma;
}

#define USB_SUSPEND_BIT	BIT(0)
static int
qpnp_chg_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	return qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_SUSP,
			USB_SUSPEND_BIT,
			enable ? USB_SUSPEND_BIT : 0, 1);
}

static int
qpnp_chg_charge_en(struct qpnp_chg_chip *chip, int enable)
{
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_CHG_EN,
			enable ? CHGR_CHG_EN : 0, 1);
}

static int
qpnp_chg_force_run_on_batt(struct qpnp_chg_chip *chip, int disable)
{
	/* Don't run on battery for batteryless hardware */
	if (chip->use_default_batt_values)
		return 0;
	/* Don't force on battery if battery is not present */
	if (!qpnp_chg_is_batt_present(chip))
		return 0;

	/* This bit forces the charger to run off of the battery rather
	 * than a connected charger */
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_ON_BAT_FORCE_BIT,
			disable ? CHGR_ON_BAT_FORCE_BIT : 0, 1);
}

#define BUCK_DUTY_MASK_100P	0x30
static int
qpnp_buck_set_100_duty_cycle_enable(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("enable: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_err("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_TEST_SMBC_MODES,
			BUCK_DUTY_MASK_100P, enable ? 0x00 : 0x10, 1);
	if (rc) {
		pr_err("failed enable 100p duty cycle rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define COMPATATOR_OVERRIDE_0	0x80
static int
qpnp_chg_toggle_chg_done_logic(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("toggle: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_err("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_1,
			0xC0, enable ? 0x00 : COMPATATOR_OVERRIDE_0, 1);
	if (rc) {
		pr_err("failed to toggle chg done override rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define QPNP_CHG_VBATDET_MIN_MV	3240
#define QPNP_CHG_VBATDET_MAX_MV	5780
#define QPNP_CHG_VBATDET_STEP_MV	20
static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv)
{
	u8 temp;

	if (vbatdet_mv < QPNP_CHG_VBATDET_MIN_MV
			|| vbatdet_mv > QPNP_CHG_VBATDET_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", vbatdet_mv);
		return -EINVAL;
	}
	temp = (vbatdet_mv - QPNP_CHG_VBATDET_MIN_MV)
			/ QPNP_CHG_VBATDET_STEP_MV;

	pr_info("voltage=%d setting %02x\n", vbatdet_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_DET, 1);
}

#if !(defined(CONFIG_HTC_BATT_8960))
static void
qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
			- chip->resume_delta_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
			- chip->resume_delta_mv);
	else if (chip->resuming_charging)
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			+ chip->resume_delta_mv);
	else
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			- chip->resume_delta_mv);
}
#else
static int
qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip)
{
	int rc = 0, vbat = 0;

	wake_lock(&chip->set_vbatdet_lock);
	if (chip->bat_is_cool)
		vbat = chip->cool_bat_mv - chip->resume_delta_mv;
	else if (chip->bat_is_warm)
		vbat = chip->warm_bat_mv - chip->resume_delta_mv;
	else if (is_batt_full_eoc_stop)
		vbat = chip->max_voltage_mv - chip->resume_delta_mv;
	else /* Set max value when not charging and not over temp */
		vbat = QPNP_CHG_VBATDET_MAX_MV;

	rc = qpnp_chg_vbatdet_set(chip, vbat);

	if (rc)
		pr_err("Failed to set vbatdet=%d rc=%d\n", vbat, rc);
	else
		pr_info("vbatdet=%d, bat_is_cool=%d, bat_is_warm=%d\n", vbat, chip->bat_is_cool, chip->bat_is_warm);

	/* reset charger after re-configure vbat_det setting
	   due to fastchg irq trigger failure is observed. */
	qpnp_chg_charge_en(chip, false);
	/* sleep for a second before enabling */
	msleep(2000);
	qpnp_chg_charge_en(chip, true);
	wake_unlock(&chip->set_vbatdet_lock);

	return rc;
}
#endif /* !CONFIG_HTC_BATT_8960 */

static void
qpnp_arb_stop_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, arb_stop_work);
	struct qpnp_vadc_result result;
	u8 usb_sts;
	int usbin, rc_usb_sts, rc_usbin;

	rc_usb_sts = qpnp_chg_read(chip, &usb_sts, INT_RT_STS(chip->usb_chgpth_base), 1);
	if (rc_usb_sts)
		pr_err("failed to read usb_chgpth_sts rc=%d\n", rc_usb_sts);

	rc_usbin = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
	if (rc_usbin)
		pr_warn("error reading USBIN channel = %d, rc = %d\n",
					USBIN, rc_usbin);
	usbin = (int)result.physical;

	if (!chip->chg_done)
		qpnp_chg_charge_en(chip, !chip->charging_disabled);
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	wake_unlock(&chip->reverse_boost_wa_wake_lock);

	pr_info("usb_chgpth_sts:0x%X, usbin=%d(uV)\n", usb_sts, usbin);
	if (!rc_usb_sts &&
		qpnp_chg_is_usb_chg_plugged_in(chip) &&
		(usb_sts & CHG_GONE_IRQ)) {
		wake_lock_timeout(&chip->reverse_boost_wa_wake_lock, 2*HZ);
		schedule_delayed_work(&chip->fix_reverse_boost_check_work, 0);
	}
}

#if !(defined(CONFIG_HTC_BATT_8960))
static void
qpnp_bat_if_adc_measure_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_measure_work);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

static void
qpnp_bat_if_adc_disable_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_disable_work);

	qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev, &chip->adc_param);
}
#endif /* !CONFIG_HTC_BATT_8960 */

static int charger_monitor;
module_param(charger_monitor, int, 0644);

#define EOC_CHECK_PERIOD_MS	10000
static irqreturn_t
qpnp_chg_vbatdet_lo_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chg_sts = 0;
	int rc;

	pr_info("[irq]vbatdet-lo triggered\n");

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read chg_sts rc=%d\n", rc);

	pr_info("chg_done chg_sts: 0x%x triggered\n", chg_sts);
	if (!chip->charging_disabled && (chg_sts & FAST_CHG_ON_IRQ)) {
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		pm_stay_awake(chip->dev);
	}
		qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);

#if !(defined(CONFIG_HTC_BATT_8960))
	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
#endif
	return IRQ_HANDLED;
}

#define REVERSE_BOOST_CHECK_PERIOD_MS	200
#define ARB_STOP_WORK_MS	1000
static irqreturn_t
qpnp_chg_usb_chg_gone_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 usb_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usb_sts, INT_RT_STS(chip->usb_chgpth_base), 1);
	if (rc)
		pr_err("failed to read usb_chgpth_sts rc=%d\n", rc);

	pr_info("[irq]chg_gone triggered, usb_sts:0x%X\n", usb_sts);

	if (qpnp_chg_is_usb_chg_plugged_in(chip) && (usb_sts & CHG_GONE_IRQ)) {
		wake_lock_timeout(&chip->reverse_boost_wa_wake_lock, 2*HZ);
		schedule_delayed_work(&chip->fix_reverse_boost_check_work, msecs_to_jiffies(REVERSE_BOOST_CHECK_PERIOD_MS));
	}

	return IRQ_HANDLED;
}

#define ENABLE_REVERSE_BOOST_WA_THR_UV	4700000
static void
qpnp_fix_reverse_boost_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, fix_reverse_boost_check_work);
	struct qpnp_vadc_result result;
	int usbin, vchg, rc;
	u8 chgpth_set_type, chgpth_polarity_high, chg_gone_int;

	rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
	if (rc)
		pr_warn("error reading USBIN channel = %d, rc = %d\n",
					USBIN, rc);
	usbin = (int)result.physical;
	rc = qpnp_vadc_read(chip->vadc_dev, VCHG_SNS, &result);
	if (rc)
		pr_warn("error reading VCHG channel = %d, rc = %d\n",
					VCHG_SNS, rc);
	vchg = (int)result.physical;
	rc = qpnp_chg_read(chip, &chgpth_set_type,
					chip->usb_chgpth_base + CHGPTH_INT_SET_TYPE , 1);
	rc = qpnp_chg_read(chip, &chgpth_polarity_high,
					chip->usb_chgpth_base + CHGPTH_INT_POLARITY_HIGH , 1);
	rc = qpnp_chg_read(chip, &chg_gone_int,
					chip->usb_chgpth_base + CHGPTH_CHG_GONE_INT , 1);
	pr_info("usbin=%d(uV), vchg=%d(uV), set_type=0x%x, ply_high=0x%x, "
			"chg_gone_int=0x%x\n",
		usbin, vchg, chgpth_set_type, chgpth_polarity_high,chg_gone_int);
	if (usbin < ENABLE_REVERSE_BOOST_WA_THR_UV) {
		qpnp_chg_charge_en(chip, 0);
		qpnp_chg_force_run_on_batt(chip, 1);
		schedule_delayed_work(&chip->arb_stop_work,
			msecs_to_jiffies(ARB_STOP_WORK_MS));
	} else
		wake_unlock(&chip->reverse_boost_wa_wake_lock);

}

static irqreturn_t
qpnp_chg_usb_usb_ocp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int rc;

	pr_info("[irq]usb-ocp triggered\n");

	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OCP_CLR,
			OCP_CLR_BIT,
			OCP_CLR_BIT, 1);
	if (rc)
		pr_err("Failed to clear OCP bit rc = %d\n", rc);

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc)
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);

	return IRQ_HANDLED;
}

static void update_ovp_uvp_worker(struct work_struct *work)
{
	int ov = false, uv = false, v = false;

	if (!the_chip) {
		pr_err("called before init\n");
		return;
	}

	get_prop_usb_valid_status(the_chip, &ov, &v, &uv);
	update_ovp_uvp_state(ov, v, uv);
}

static irqreturn_t
qpnp_chg_coarse_det_usb_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_info("[irq]coarse_det_usb triggered\n");

	/* The CHGR_STATUS not update immediately,
	    if read it too early, the result might fail. We delay 1s to read it. */
	schedule_delayed_work(&chip->update_ovp_uvp_work,
			msecs_to_jiffies(1000));

	return IRQ_HANDLED;
}

#define RESUME_VDDMAX_WORK_MS	2000
#define READJUST_VDDMAX_WORK_MS	30000
#define VIN_MIN_COLLAPSE_CHECK_MS	350
#define ENUM_T_STOP_BIT		BIT(0)
static irqreturn_t
qpnp_chg_usb_usbin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int usb_present, host_mode;

	usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	host_mode = qpnp_chg_is_otg_en_set(chip);
	pr_info("[irq]usb_present: %d-> %d, host_mode:%d, usb_target_ma=%d, "
			"aicl_worker=%d, is_vin_min_detected=%d\n",
		chip->usb_present, usb_present, host_mode, usb_target_ma,
		is_aicl_worker_enabled, is_vin_min_detected);

	/* In host mode notifications cmoe from USB supply */
	if (host_mode)
		return IRQ_HANDLED;

#if !(defined(CONFIG_HTC_BATT_8960))
	power_supply_set_present(chip->usb_psy, chip->usb_present);
#else
	if (usb_target_ma > USB_MA_2) {
		schedule_delayed_work(&the_chip->vin_collapse_check_work,
				      msecs_to_jiffies(VIN_MIN_COLLAPSE_CHECK_MS));
		wake_lock_timeout(&the_chip->vin_collapse_check_wake_lock, HZ/2);
	} else {
		cable_detection_vbus_irq_handler();
	}
#endif /* !CONFIG_HTC_BATT_8960 */

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_bat_if_batt_temp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_temp_good;
#ifdef CONFIG_ARCH_MSM8226
	int rc;
#endif

	batt_temp_good = qpnp_chg_is_batt_temp_ok(chip);
	pr_info("[irq]batt-temp triggered: %d\n", batt_temp_good);

#ifdef CONFIG_ARCH_MSM8226
	rc = qpnp_chg_masked_write(chip, chip->buck_base + SEC_ACCESS, 0xFF, 0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip, chip->buck_base + TEST_EN_SMBC_LOOP, IBAT_REGULATION_DISABLE, batt_temp_good ? 0 : IBAT_REGULATION_DISABLE, 1);
	if (rc) {
		pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
		return rc;
	}
#endif
#if !(defined(CONFIG_HTC_BATT_8960))
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_bat_if_batt_pres_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_present;

	batt_present = qpnp_chg_is_batt_present(chip);
	pr_info("[irq]batt-pres triggered: %d\n", batt_present);

	if (chip->batt_present ^ batt_present) {
		chip->batt_present = batt_present;
#if !(defined(CONFIG_HTC_BATT_8960))
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
		pr_debug("psy changed usb_psy\n");
		power_supply_changed(chip->usb_psy);

		if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& batt_present) {
			pr_debug("enabling vadc notifications\n");
			schedule_work(&chip->adc_measure_work);
		} else if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
				&& !batt_present) {
			schedule_work(&chip->adc_disable_work);
			pr_debug("disabling vadc notifications\n");
		}
#endif
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_dc_dcin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int dc_present;

	dc_present = qpnp_chg_is_dc_chg_plugged_in(chip);
	pr_info("[irq]dcin-valid triggered: %d\n", dc_present);

	if (chip->dc_present ^ dc_present) {
		chip->dc_present = dc_present;
		if (qpnp_chg_is_otg_en_set(chip))
			qpnp_chg_force_run_on_batt(chip, !dc_present ? 1 : 0);
		if (!dc_present) {
			chip->chg_done = false;
			is_batt_full = false;
			is_batt_full_eoc_stop = false;
		} else {
			pm_stay_awake(chip->dev);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		}
#if !(defined(CONFIG_HTC_BATT_8960))
		power_supply_changed(&chip->dc_psy);
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
#endif
	}

	return IRQ_HANDLED;
}

#define CHGR_CHG_FAILED_BIT	BIT(7)
static irqreturn_t
qpnp_chg_chgr_chg_failed_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int rc;

	pr_info("[irq]chg_failed triggered\n");

	if (!is_ac_online() || flag_keep_charge_on || flag_pa_recharge
		|| (get_kernel_flag() & KERNEL_FLAG_ENABLE_FAST_CHARGE)) {
		pr_info("write CHG_FAILED_CLEAR bit\n");
		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_CHG_FAILED,
			CHGR_CHG_FAILED_BIT,
			CHGR_CHG_FAILED_BIT, 1);

		if (rc)
			pr_err("Failed to write chg_fail clear bit!\n");
	} else {
		if ((chip->tchg_mins > SAFETY_TIME_MAX_LIMIT) &&
				!is_ac_safety_timeout_twice) {
			is_ac_safety_timeout_twice = true;
			pr_info("%s: write CHG_FAILED_CLEAR bit "
					"due to safety time is twice\n", __func__);
			rc = qpnp_chg_masked_write(chip,
				chip->chgr_base + CHGR_CHG_FAILED,
				CHGR_CHG_FAILED_BIT,
				CHGR_CHG_FAILED_BIT, 1);

			if (rc)
				pr_err("Failed to write chg_fail clear bit!\n");
		} else {
			is_ac_safety_timeout = true;
			pr_err("batt_present=%d, batt_temp_ok=%d, state_changed_to=%d\n",
					get_prop_batt_present(chip),
					qpnp_chg_is_batt_temp_ok(chip),
					pm8941_get_chgr_fsm_state(chip));
			htc_charger_event_notify(HTC_CHARGER_EVENT_SAFETY_TIMEOUT);
		}
	}

#if !(defined(CONFIG_HTC_BATT_8960))
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
#endif
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_trklchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_info("[irq]TRKL IRQ triggered\n");

	chip->chg_done = false;
#if !(defined(CONFIG_HTC_BATT_8960))
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
#endif

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_fastchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chgr_sts;
	int rc;

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read interrupt sts %d\n", rc);

	pr_info("[irq]FAST_CHG IRQ triggered\n");
	chip->chg_done = false;
	/* Update battery uevent for QCT charger_monitor daemon
	     because we don't receive fastchg_irq immediately after setting charging. */
	if (charger_monitor == 9)
		htc_charger_event_notify(HTC_CHARGER_EVENT_BATT_UEVENT_CHANGE);

#if !(defined(CONFIG_HTC_BATT_8960))
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}

	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);

	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}

	if (chip->resuming_charging) {
		chip->resuming_charging = false;
		qpnp_chg_set_appropriate_vbatdet(chip);
	}
#endif

	if (!chip->charging_disabled) {
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		pm_stay_awake(chip->dev);
		/* Set MAX recharge voltage */
		pr_info("Set vbatdet=%d before recharge is started\n",
				QPNP_CHG_VBATDET_MAX_MV);
		rc = qpnp_chg_vbatdet_set(chip, QPNP_CHG_VBATDET_MAX_MV);
		if (rc)
			pr_err("Failed to set vbatdet=%d rc=%d\n",
					QPNP_CHG_VBATDET_MAX_MV, rc);
	}

	qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);

	return IRQ_HANDLED;
}

#if !(defined(CONFIG_HTC_BATT_8960))
static int
qpnp_dc_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_batt_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_WARM_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_chg_buck_control(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	if (chip->charging_disabled && enable) {
		pr_debug("Charging disabled\n");
		return 0;
	}

	rc = qpnp_chg_charge_en(chip, enable);
	if (rc) {
		pr_err("Failed to control charging %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_force_run_on_batt(chip, !enable);
	if (rc)
		pr_err("Failed to control charging %d\n", rc);

	return rc;
}
#endif /* !CONFIG_HTC_BATT_8960 */

static int
switch_usb_to_charge_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_info("switch to charge mode\n");
	if (!qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->otg_en_gpio && gpio_is_valid(chip->otg_en_gpio))
		gpio_set_value(chip->otg_en_gpio, 0);

	/* enable usb ovp fet */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			0, 1);
	if (rc) {
		pr_err("Failed to turn on usb ovp rc = %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	if (rc) {
		pr_err("Failed re-enable charging rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int
switch_usb_to_host_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_info("switch to host mode\n");
	if (qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->otg_en_gpio && gpio_is_valid(chip->otg_en_gpio))
		gpio_set_value(chip->otg_en_gpio, 1);

	if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_force_run_on_batt(chip, 1);
		if (rc) {
			pr_err("Failed to disable charging rc = %d\n", rc);
			return rc;
		}
	}

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc) {
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);
		return rc;
	}
	return 0;
}

#if !(defined(CONFIG_HTC_BATT_8960))
static enum power_supply_property pm_power_props_mains[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_COOL_TEMP,
	POWER_SUPPLY_PROP_WARM_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
};

static char *pm_power_supplied_to[] = {
	"battery",
};

static char *pm_batt_supplied_to[] = {
	"bms",
};



static int
qpnp_power_get_property_mains(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (chip->charging_disabled)
			return 0;

		val->intval = qpnp_chg_is_dc_chg_plugged_in(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->maxinput_dc_ma * 1000;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void
qpnp_aicl_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, aicl_check_work);
	union power_supply_propval ret = {0,};

	if (!charger_monitor && qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		if ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA) {
			pr_debug("no charger_monitor present set iusbmax %d\n",
					ret.intval / 1000);
			qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		}
	} else {
		pr_debug("charger_monitor is present\n");
	}
	chip->charger_monitor_checked = true;
}
#endif /* !CONFIG_HTC_BATT_8960 */

#define USB_WALL_THRESHOLD_MA	500
static int
get_prop_battery_voltage_now(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->revision == 0 && chip->type == SMBB) {
		pr_err("vbat reading not supported for 1.0 rc=%d\n", rc);
		return 0;
	} else {
		rc = qpnp_vadc_read(chip->vadc_dev, VBAT_SNS, &results);
		if (rc) {
			pr_err("Unable to read vbat rc=%d\n", rc);
			return 0;
		}
		return results.physical;
	}
}

#define BATT_PRES_BIT BIT(7)
static int
get_prop_batt_present(struct qpnp_chg_chip *chip)
{
	u8 batt_present;
	int rc;

	rc = qpnp_chg_read(chip, &batt_present,
				chip->bat_if_base + CHGR_BAT_IF_PRES_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery status read failed rc=%d\n", rc);
		return 0;
	};
	return (batt_present & BATT_PRES_BIT) ? 1 : 0;
}

#define BATT_TEMP_HOT	BIT(6)
#define BATT_TEMP_OK	BIT(7)
static int
get_prop_batt_health(struct qpnp_chg_chip *chip)
{
	u8 batt_health;
	int rc;

	rc = qpnp_chg_read(chip, &batt_health,
				chip->bat_if_base + CHGR_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery health read failed rc=%d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	};

	if (BATT_TEMP_OK & batt_health)
		return POWER_SUPPLY_HEALTH_GOOD;
	if (BATT_TEMP_HOT & batt_health)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		return POWER_SUPPLY_HEALTH_COLD;
}

static int
get_prop_charge_type(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts;

	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	if (chgr_sts & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}

static int
get_prop_batt_status(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts, bat_if_sts;

	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip)) && chip->chg_done) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	rc = qpnp_chg_read(chip, &bat_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read bat_if sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ && bat_if_sts & BAT_FET_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;
	if (chgr_sts & FAST_CHG_ON_IRQ && bat_if_sts & BAT_FET_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static u8 pm_get_chgr_int_rt_sts(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts;

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);

	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return rc;
	}

	return chgr_sts;
}

static u8 pm_get_buck_int_rt_sts(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 buck_sts;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck rc=%d\n", rc);
		return rc;
	}

	return buck_sts;
}

static u8 pm_get_bat_if_int_rt_sts(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 batt_if_sts;

	rc = qpnp_chg_read(chip, &batt_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return rc;
	}
	return batt_if_sts;
}

static int is_ac_online(void)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}
	return qpnp_chg_is_usb_chg_plugged_in(the_chip) &&
				(pwr_src == HTC_PWR_SOURCE_TYPE_AC ||
							pwr_src == HTC_PWR_SOURCE_TYPE_9VAC ||
							pwr_src == HTC_PWR_SOURCE_TYPE_MHL_AC);
}

#if !(defined(CONFIG_HTC_BATT_8960))
static int
get_prop_current_now(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_full_design(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_charge_full(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

#define DEFAULT_CAPACITY	50
static int
get_prop_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};
	int battery_status, bms_status, soc, charger_in;

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		soc = ret.intval;
		battery_status = get_prop_batt_status(chip);
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_STATUS, &ret);
		bms_status = ret.intval;
		charger_in = qpnp_chg_is_usb_chg_plugged_in(chip) ||
			qpnp_chg_is_dc_chg_plugged_in(chip);

		if (battery_status != POWER_SUPPLY_STATUS_CHARGING
				&& bms_status != POWER_SUPPLY_STATUS_CHARGING
				&& charger_in
				&& !chip->resuming_charging
				&& !chip->charging_disabled
				&& chip->soc_resume_limit
				&& soc <= chip->soc_resume_limit) {
			pr_debug("resuming charging at %d%% soc\n", soc);
			chip->resuming_charging = true;
			qpnp_chg_set_appropriate_vbatdet(chip);
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		if (soc == 0) {
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)
				&& !qpnp_chg_is_usb_chg_plugged_in(chip))
				pr_warn_ratelimited("Battery 0, CHG absent\n");
		}
		return soc;
	} else {
		pr_debug("No BMS supply registered return 50\n");
	}

	/* return default capacity to avoid userspace
	 * from shutting down unecessarily */
	return DEFAULT_CAPACITY;
}
#endif /* !CONFIG_HTC_BATT_8960 */

#define DEFAULT_TEMP		250
#define MAX_TOLERABLE_BATT_TEMP_DDC	680
static int
get_prop_batt_temp(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

#if !(defined(CONFIG_HTC_BATT_8960)) /* to report real battery temp for debugging */
	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_TEMP;
#endif

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX1_BATT_THERM, &results);
	if (rc) {
		pr_err("Unable to read batt temperature rc=%d\n", rc);
		return 0;
	}
	pr_debug("get_bat_temp %d %lld\n",
		results.adc_code, results.physical);

	if ((results.physical >= 680) &&
			(flag_keep_charge_on || flag_pa_recharge))
			results.physical = 650;

	return (int)results.physical;
}

static int get_prop_vchg_loop(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & VCHG_LOOP_IRQ) ? 1 : 0;
}

#if !(defined(CONFIG_HTC_BATT_8960))
static int get_prop_cycle_count(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy)
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CYCLE_COUNT, &ret);
	return ret.intval;
}

static int get_prop_online(struct qpnp_chg_chip *chip)
{
	return qpnp_chg_is_batfet_closed(chip);
}

static void
qpnp_batt_external_power_changed(struct power_supply *psy)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	union power_supply_propval ret = {0,};

	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");

	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_ONLINE, &ret);

	/* Only honour requests while USB is present */
	if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

		if (chip->prev_usb_max_ma == ret.intval)
			goto skip_set_iusb_max;

		if (ret.intval <= 2 && !chip->use_default_batt_values &&
						get_prop_batt_present(chip)) {
			qpnp_chg_usb_suspend_enable(chip, 1);
			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
		} else {
			qpnp_chg_usb_suspend_enable(chip, 0);
			if (((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
					&& (charger_monitor ||
					!chip->charger_monitor_checked)) {
				qpnp_chg_iusbmax_set(chip,
						USB_WALL_THRESHOLD_MA);
			} else {
				qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
			}

			if ((chip->flags & POWER_STAGE_WA)
			&& ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
			&& !chip->power_stage_workaround_running
			&& chip->power_stage_workaround_enable) {
				chip->power_stage_workaround_running = true;
				pr_debug("usb wall chg inserted starting power stage workaround charger_monitor = %d\n",
					charger_monitor);
				schedule_work(&chip->reduce_power_stage_work);
			}
		}
		chip->prev_usb_max_ma = ret.intval;
	}

skip_set_iusb_max:
	pr_debug("end of power supply changed\n");
#if !(defined(CONFIG_HTC_BATT_8960))
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
}

static int
qpnp_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->max_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->min_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_prop_battery_voltage_now(chip);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_prop_batt_temp(chip);
		break;
	case POWER_SUPPLY_PROP_COOL_TEMP:
		val->intval = chip->cool_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		val->intval = chip->warm_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_capacity(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_prop_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = get_prop_full_design(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = get_prop_charge_full(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !(chip->charging_disabled);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = get_prop_cycle_count(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		val->intval = get_prop_vchg_loop(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = qpnp_chg_usb_iusbmax_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = qpnp_chg_vinmin_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = get_prop_online(chip);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
#else /* CONFIG_HTC_BATT_8960 */

/* assumes vbus_lock is held */
static void __pm8941_charger_vbus_draw(unsigned int mA)
{
	int rc;
	if (mA >= 0 && mA <= 2) {
		rc = qpnp_chg_usb_suspend_enable(the_chip, TRUE);
		if (rc)
			pr_err("fail to set suspend bit rc=%d\n", rc);

		rc = qpnp_chg_iusbmax_set(the_chip, QPNP_CHG_I_MAX_MIN_100);
		if (rc) {
			pr_err("unable to set iusb to %d rc = %d\n", 0, rc);
		}
	} else {
		rc = qpnp_chg_usb_suspend_enable(the_chip, FALSE);
		if (rc)
			pr_err("fail to reset suspend bit rc=%d\n", rc);
		rc = qpnp_chg_iusbmax_set(the_chip, mA);
		if (rc) {
			pr_err("unable to set iusb to %d rc = %d\n", 0, rc);
		}
	}
}

static DEFINE_SPINLOCK(vbus_lock);
/* assumes vbus_lock is held */
static void _pm8941_charger_vbus_draw(unsigned int mA)
{
	unsigned long flags;

	if (!the_chip) {
		pr_err("called before init\n");
		return;
	}

	if (usb_target_ma <= USB_MA_2 && mA > usb_wall_threshold_ma
			&& !hsml_target_ma) {
		usb_target_ma = mA;
		is_usb_target_ma_changed_by_qc20 = false;
	}

	spin_lock_irqsave(&vbus_lock, flags);

	if (mA > QPNP_CHG_I_MAX_MAX_MA) {
		__pm8941_charger_vbus_draw(QPNP_CHG_I_MAX_MAX_MA);
	} else {
		if (!hsml_target_ma
				&& the_chip->is_pm8921_aicl_enabled
				&& mA > usb_wall_threshold_ma)
			__pm8941_charger_vbus_draw(usb_wall_threshold_ma);
		else
			__pm8941_charger_vbus_draw(mA);
	}

	spin_unlock_irqrestore(&vbus_lock, flags);
}

static u32 htc_fake_charger_for_testing(enum htc_power_source_type src)
{
	/* set charger to 1A AC  by default */
	enum htc_power_source_type new_src = HTC_PWR_SOURCE_TYPE_AC;

	if((src > HTC_PWR_SOURCE_TYPE_9VAC) || (src == HTC_PWR_SOURCE_TYPE_BATT))
		return src;

	pr_info("%s(%d -> %d)\n", __func__, src , new_src);
	return new_src;
}

/* htc: to protect BAT_FET sw enable/disable contorl cs */
static DEFINE_SPINLOCK(charger_lock);
static int batt_charging_disabled; /* BAT FET side */
/* sw disabled batt fet control mask (reason) */
#define BATT_CHG_DISABLED_BIT_EOC	(1)
#define BATT_CHG_DISABLED_BIT_KDRV	(1<<1)
#define BATT_CHG_DISABLED_BIT_USR1	(1<<2)
#define BATT_CHG_DISABLED_BIT_USR2	(1<<3)
#define BATT_CHG_DISABLED_BIT_BAT	(1<<4)

static int pm_chg_disable_auto_enable(struct qpnp_chg_chip *chip,
		int disable, int reason)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&charger_lock, flags);
	if (disable)
		batt_charging_disabled |= reason;	/* set bit */
	else
		batt_charging_disabled &= ~reason;	/* clr bit */
	rc = qpnp_chg_charge_en(the_chip, !batt_charging_disabled);
	if (rc)
		pr_err("Failed rc=%d\n", rc);
	spin_unlock_irqrestore(&charger_lock, flags);

	return rc;
}

static DEFINE_SPINLOCK(pwrsrc_lock);
/* sw disabled vbus power source control mask (reason) */
#define PWRSRC_DISABLED_BIT_KDRV	(1)
#define PWRSRC_DISABLED_BIT_USER	(1<<1)
#define PWRSRC_DISABLED_BIT_AICL		(1<<2)

static int pwrsrc_disabled; /* USBIN side */

static int pm_chg_disable_pwrsrc(struct qpnp_chg_chip *chip,
		int disable, int reason)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&pwrsrc_lock, flags);
	if (disable)
		pwrsrc_disabled |= reason;	/* set bit */
	else
		pwrsrc_disabled &= ~reason;	/* clr bit */
	rc = qpnp_chg_force_run_on_batt(chip, pwrsrc_disabled);
	if (rc)
		pr_err("Failed rc=%d\n", rc);
	spin_unlock_irqrestore(&pwrsrc_lock, flags);

	return rc;
}

/* htc_charger interface */
int pm8941_is_pwr_src_plugged_in(void)
{
	int usb_in, dc_in;

	/* Check if called before init */
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	usb_in = qpnp_chg_is_usb_chg_plugged_in(the_chip);
	dc_in = qpnp_chg_is_dc_chg_plugged_in(the_chip);
	pr_info("%s: usb_in=%d, dc_in=%d\n", __func__, usb_in, dc_in);
	if (usb_in ^ dc_in)
		return TRUE;
	else if (usb_in & dc_in)
		pr_warn("%s: Abnormal interrupt due to usb_in & dc_in"
				" is existed together\n", __func__);
	return FALSE;
}

int adjust_chg_vin_min(struct qpnp_chg_chip *chip,
				int only_increased)
{
	int rc = 0, vbat_mv, ori_vin_min, target_vin_min;

	vbat_mv = get_prop_battery_voltage_now(chip)/1000;
	target_vin_min = ori_vin_min = qpnp_chg_vinmin_get(chip);

	if (vbat_mv >= chip->regulate_vin_min_thr_mv)
		target_vin_min = VIN_MIN_4400_MV;
	else {
		if (!only_increased)
			target_vin_min = chip->lower_vin_min;
	}

	if (target_vin_min != ori_vin_min) {
		rc = qpnp_chg_vinmin_set(chip, target_vin_min);
		if (rc)
			pr_err("%s: Failed to set vin min, vbat_mv=%d mV rc=%d\n",
							__func__, vbat_mv, rc);
		pr_info(": vbat_mv=%d, target_vin_min=%d, ori_vin_min=%d\n",
			vbat_mv, target_vin_min, ori_vin_min);
	}

	chip->min_voltage_mv = qpnp_chg_vinmin_get(chip);

	return rc;
}

struct usb_ma_limit_entry {
	int usb_ma;
};

static struct usb_ma_limit_entry usb_ma_table[] = {
	{100},
	{500},
	{700},
	{800},
	{900},
	{1000},
	{1100},
	{1200},
	{1300},
	{1400},
	{1500},
	{1600},
};

static int find_usb_ma_value(int value)
{
	int i;

	for (i = ARRAY_SIZE(usb_ma_table) - 1; i >= 0; i--) {
		if (value >= usb_ma_table[i].usb_ma)
			break;
	}

	return i;
}

static void decrease_usb_ma_value(int *value)
{
	int i;

	if (value) {
		i = find_usb_ma_value(*value);
		if (i > 0)
			i--;
		if (i >= 0)
			*value = usb_ma_table[i].usb_ma;
	}
}

static void increase_usb_ma_value(int *value)
{
	int i;

	if (value) {
		i = find_usb_ma_value(*value);

		if (i < (ARRAY_SIZE(usb_ma_table) - 1))
			i++;
		*value = usb_ma_table[i].usb_ma;
	}
}

#define AICL_CHECK_RAMP_MS 	(200)
static void vin_collapse_check_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
			struct qpnp_chg_chip, vin_collapse_check_work);

	/*
	 * AICL only for wall-chargers. If the charger appears to be plugged
	 * back in now, the corresponding unplug must have been because of we
	 * were trying to draw more current than the charger can support. In
	 * such a case reset usb current to 500mA and decrease the target.
	 * The AICL algorithm will step up the current from 500mA to target
	 */
	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			usb_target_ma >= usb_wall_threshold_ma) {
		/* decrease usb_target_ma */
		decrease_usb_ma_value(&usb_target_ma);
		/* reset here, increase in unplug_check_worker */
		__pm8941_charger_vbus_draw(usb_wall_threshold_ma);
		pr_info("usb_now=%d, usb_target=%d, is_vin_min_detected=%d\n",
			usb_wall_threshold_ma, usb_target_ma,
			is_vin_min_detected);
		if (!delayed_work_pending(&chip->aicl_check_work))
			queue_delayed_work(chip->aicl_check_wq, &chip->aicl_check_work,
				msecs_to_jiffies(AICL_CHECK_RAMP_MS));
	} else {
		cable_detection_vbus_irq_handler();
	}
}

#define RETRY_AICL_TOT_COUNT			(2)
#define RETRY_AICL_TIME				(300000)
#define VIN_MIN_DETECT_DURATION_MS		(1500)
#define AICL_CHECK_WAIT_PERIOD_MS 		(200)
#define QC20_9_VOLT_CHECK			(8000000)
#define QC20_VIN_MIN_MV				(8200)
static void retry_aicl_mechanism(struct qpnp_chg_chip *chip)
{
	unsigned long time_since_last_update_ms, cur_jiffies;
	struct qpnp_vadc_result result;
	int usbin = 0, rc = 0;

	cur_jiffies = jiffies;
	time_since_last_update_ms =
		(cur_jiffies - retry_aicl_timer.last_do_jiffies) * MSEC_PER_SEC / HZ;
	retry_aicl_timer.total_time_ms += time_since_last_update_ms;
	retry_aicl_timer.last_do_jiffies = cur_jiffies;

	if (retry_aicl_timer.total_time_ms > RETRY_AICL_TIME) {
		rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
		if (rc)
			pr_err("AICL: error reading USBIN channel = %d, rc = %d\n",
						USBIN, rc);
		usbin = (int)result.physical;
		usb_target_ma = aicl_target_ma;
		pr_info("AICL: retry triggered, total_ms=%ld, usbin=%d, usb_target_ma=%d\n",
			retry_aicl_timer.total_time_ms, usbin, usb_target_ma);
		retry_aicl_timer.total_time_ms = 0;
		is_usb_target_ma_changed_by_qc20 = false;
		queue_delayed_work(chip->aicl_check_wq, &chip->aicl_check_work,
			msecs_to_jiffies(AICL_CHECK_WAIT_PERIOD_MS));
	}
}

#define ADJUST_VDDMAX_VALUE	100
static int decrease_vddmax_configure_work(void)
{
	int vbat_mv;

	/* Check if called before init */
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	vbat_mv = (get_prop_battery_voltage_now(the_chip)/1000);
	pr_info("vbatt_mv:%d\n", vbat_mv);
	if (vbat_mv + ADJUST_VDDMAX_VALUE <= the_chip->max_voltage_mv)
		qpnp_chg_vddmax_set(the_chip, max((vbat_mv + ADJUST_VDDMAX_VALUE), QPNP_CHG_VDDMAX_MIN));
	else
		qpnp_chg_vddmax_set(the_chip, the_chip->max_voltage_mv);

	return 0;
}

static void resume_vddmax_configure_work(struct work_struct *work)
{
	/* Check if called before init */
	if (!the_chip)
		pr_warn("called before init\n");
	else {
		pr_info("resume vddmax for display flicker issue\n");
		qpnp_chg_set_appropriate_vddmax(the_chip);
	}
}

static void readjust_vddmax_configure_work(struct work_struct *work)
{
	int ibat_ma = 0;

	/* Check if called before init */
	if (!the_chip)
		pr_warn("called before init\n");
	else {
		if (the_chip->bat_is_cool || the_chip->bat_is_warm) {
			pr_info("Not readjust vddmax, warm=%d, cool=%d\n",
					the_chip->bat_is_warm, the_chip->bat_is_cool);
			return;
		}
		pm8941_bms_get_batt_current(&ibat_ma);
		ibat_ma /= 1000;
		pr_info("readjust vddmax to cover pmic tolerance issue, "
				"ibat_ma=%d, pre_delta_vddmax=%d\n",
				ibat_ma, the_chip->pre_delta_vddmax_mv);
		if (qpnp_chg_is_usb_chg_plugged_in(the_chip)
				&& ibat_ma > READJUST_VDDMAX_THR_MA
				&& the_chip->pre_delta_vddmax_mv != 0) {
			the_chip->delta_vddmax_mv =
				the_chip->pre_delta_vddmax_mv;
			qpnp_chg_set_appropriate_vddmax(the_chip);
		}
	}
}

#define OVPFET_R_CHECK_WAIT_MS_FIRST 	(10*1000) //10s
#define OVPFET_R_CHECK_WAIT_MS	(1000) //1s
#define OVPFET_R_CHECK_PERIOD_MS	(5*60*1000) //5min
#define OVPFET_R_STEP_0	(0)
#define OVPFET_R_STEP_1	(1)
#define OVPFET_R_STEP_2	(2)
#define OVPFET_R_STEP_3	(3)
#define OVPFET_R_STEP_4	(4)
#define OVPFET_R_SAMPLE_RATE	(10)
#define OVPFET_I_SET_1	USB_MA_500
#define OVPFET_I_SET_2	USB_MA_300
static int iusbtrim_ori = 0;
static int ovpfet_r_check_step = OVPFET_R_STEP_0;
static void
disable_ovpfet_work(struct qpnp_chg_chip *chip)
{
	gpio_set_value(chip->ext_ovpfet_gpio, 0);

	pr_info("AC=%d, iusbtrim_ori=%d\n", is_ac_online(), iusbtrim_ori);
	if (delayed_work_pending(&chip->ovpfet_resistance_check_work)) {
		pr_info("cancel_delayed_work ovpfet_resistance_wq.\n");
		cancel_delayed_work(&chip->ovpfet_resistance_check_work);
	}

	if (is_ac_online())
		qpnp_chg_iusbmax_set(chip, USB_MA_1100);

	// set IUSB trim back
	if (iusbtrim_ori != 0) {
		qpnp_chg_iusbtrim_set(chip , iusbtrim_ori);
		ovpfet_r_check_step = OVPFET_R_STEP_0;
	}
}

static void
calculate_new_iusbmax_iusbtrim(int delta_v, int *iusbmax, int *iusbtrim)
{
	int temp = 0, Rint = 0, IUSBint = 0;
	int EIUSB = 950; // Tolerance of IUSB setting and actual IUSB
	int Rext = 0;

	if (!the_chip) {
		pr_err("called before init\n");
		return;
	}

	temp = get_prop_batt_temp(the_chip);

	if (temp < the_chip->ext_ovpfet_temp_cool)
		Rext = the_chip->ext_ovpfet_rext_cool;
	else
		Rext = the_chip->ext_ovpfet_rext;

	pr_info("temp: %d, Rext: %d\n", temp, Rext);

	Rint = delta_v/(OVPFET_I_SET_2 - OVPFET_I_SET_1);
	Rint = Rint * 1000/EIUSB;
	IUSBint = USB_MA_1100 * Rext/(Rint + Rext);
	IUSBint = IUSBint * 1000/EIUSB;

	*iusbmax = (IUSBint/QPNP_CHG_I_MAXSTEP_MA)*QPNP_CHG_I_MAXSTEP_MA;
	*iusbtrim = iusbtrim_ori + (IUSBint%QPNP_CHG_I_MAXSTEP_MA);
	pr_info("Rint=%d, IUSBint=%d, iusbtrim_ori=%d, iusbtrim_set=%d\n",
				Rint, IUSBint, iusbtrim_ori, *iusbtrim);

	if (*iusbtrim > QPNP_CHG_I_TRIM_MAX) { // The IUSB_TRIM set range is 0x00~0x3F
		int re_cal_iusbtrim;
		re_cal_iusbtrim = iusbtrim_ori - (QPNP_CHG_I_MAXSTEP_MA
							- IUSBint%QPNP_CHG_I_MAXSTEP_MA);
		pr_info("iusbtrim: %d, re_cal_iusbtrim: %d\n", *iusbtrim, re_cal_iusbtrim);
		if (re_cal_iusbtrim > 0) {
			*iusbmax = *iusbmax + QPNP_CHG_I_MAXSTEP_MA;
			*iusbtrim = re_cal_iusbtrim;
		} else {
			*iusbtrim = QPNP_CHG_I_TRIM_MAX;
		}
	}
	return;
}

static int
calculate_vusbin_vchg(struct qpnp_chg_chip *chip, int *delta_V)
{
	int i, rc, vchg, vusbin, delta_v = 0;
	struct qpnp_vadc_result result;

	for (i = 0; i < OVPFET_R_SAMPLE_RATE; i++) {
		// read VCHG/VUSBIN to calculate deltaV1/V2
		rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
		if (rc) {
			pr_err("error reading USBIN channel = %d, rc = %d\n",
						USBIN, rc);
			return rc;
		}
		vusbin = (int)result.physical;

		rc = qpnp_vadc_read(chip->vadc_dev, VCHG_SNS, &result);
		if (rc) {
			pr_err("error reading VCHG_SNS channel = %d, rc = %d\n",
						VCHG_SNS, rc);
			return rc;
		}
		vchg = (int)result.physical;
		delta_v = delta_v + (vusbin - vchg);
	}
	*delta_V = delta_v/OVPFET_R_SAMPLE_RATE;
	return 0;

}

static void
ovpfet_resistance_check_worker(struct work_struct *work)
{
	int usb_ma, rc, iusbtrim = 0, iusbmax = 0;
	static int delta_v1 = 0, delta_v2 = 0;
	static bool first = false;
	unsigned int iusbmax_set = 0, voltage_chek_peroids = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
		struct qpnp_chg_chip, ovpfet_resistance_check_work);

	if (chip->bat_is_cool || chip->bat_is_warm || chip->charging_disabled)
		goto stop_check;

	usb_ma = qpnp_chg_usb_iusbmax_get(chip);

	if (first)
		voltage_chek_peroids = OVPFET_R_CHECK_WAIT_MS_FIRST;
	else
		voltage_chek_peroids = OVPFET_R_CHECK_WAIT_MS;

	switch (ovpfet_r_check_step) {
		case OVPFET_R_STEP_0:
			// disable OVPFET
			gpio_set_value(chip->ext_ovpfet_gpio, 0);
			// excute only when IUSB_MAX > 500
			if(usb_ma < USB_MA_500)
				return;
			// read original IUSB_TRIM
			iusbtrim_ori = qpnp_chg_usb_iusbtrim_get(chip);
			pr_info("iusb_trim_ori=0x%X\n", iusbtrim_ori);
			ovpfet_r_check_step = OVPFET_R_STEP_1;
			first = true;
			schedule_delayed_work(&chip->ovpfet_resistance_check_work, 0);
			break;
		case OVPFET_R_STEP_1:
			gpio_set_value(chip->ext_ovpfet_gpio, 0);
			iusbmax_set = OVPFET_I_SET_1;
			pr_info("qpnp_chg_iusbmax_set=%d\n", iusbmax_set);
			rc = qpnp_chg_iusbmax_set(chip, iusbmax_set);
			if (rc) {
				pr_err("set IUSB_MAX fail. Stop\n");
				goto stop_check;
			}
			ovpfet_r_check_step = OVPFET_R_STEP_2;
			schedule_delayed_work(&chip->ovpfet_resistance_check_work,
				msecs_to_jiffies(voltage_chek_peroids));
			break;
		case OVPFET_R_STEP_2:
			if(irq_read_line(chip->buck_ichg_loop.irq)) {
				rc = calculate_vusbin_vchg(chip, &delta_v1);
				if (rc) {
					pr_err("get delta_v1 fail. Stop\n");
					goto stop_check;
				}
				ovpfet_r_check_step = OVPFET_R_STEP_3;
				schedule_delayed_work(&chip->ovpfet_resistance_check_work, 0);
			} else {
				pr_info("stop due to ichg_loop.irq=%d\n",
					irq_read_line(chip->buck_ichg_loop.irq));
				goto stop_check;
			}
			break;
		case OVPFET_R_STEP_3:
			iusbmax_set = OVPFET_I_SET_2;
			pr_info("qpnp_chg_iusbmax_set=%d\n", iusbmax_set);
			rc = qpnp_chg_iusbmax_set(chip, iusbmax_set);
			if (rc) {
				pr_err("set IUSB_MAX fail. Stop\n");
				goto stop_check;
			}

			ovpfet_r_check_step = OVPFET_R_STEP_4;
			schedule_delayed_work(&chip->ovpfet_resistance_check_work,
				msecs_to_jiffies(voltage_chek_peroids));
			break;
		case OVPFET_R_STEP_4:
			if(irq_read_line(chip->buck_ichg_loop.irq)) {
				rc = calculate_vusbin_vchg(chip, &delta_v2);
				if (rc) {
					pr_err("get delta_v1 fail. Stop\n");
					goto stop_check;
				}

				calculate_new_iusbmax_iusbtrim((delta_v2 - delta_v1), &iusbmax, &iusbtrim);

				pr_info("IUSB_MAX set %d, IUSB_TRIM set %d\n", iusbmax, iusbtrim);
				qpnp_chg_iusbmax_set(chip , iusbmax);
				qpnp_chg_iusbtrim_set(chip , iusbtrim);

				gpio_set_value(chip->ext_ovpfet_gpio, 1);
				first = false;
				ovpfet_r_check_step = OVPFET_R_STEP_1;
				schedule_delayed_work(&chip->ovpfet_resistance_check_work,
						msecs_to_jiffies(OVPFET_R_CHECK_PERIOD_MS));
			} else {
				pr_info("stop due to ichg_loop.irq=%d\n",
					irq_read_line(chip->buck_ichg_loop.irq));
				goto stop_check;
			}
			break;
		default:
			pr_info("invalid type, step=%d\n", ovpfet_r_check_step);
			goto stop_check;
			break;
	}

return;

stop_check:
	disable_ovpfet_work(chip);
}

static void
aicl_check_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, aicl_check_work);
	unsigned long time_since_last_update_ms, cur_jiffies;
	struct qpnp_vadc_result result;
	int usb_ma, vchg_loop, usbin = 0, rc = 0;

	usb_ma = qpnp_chg_usb_iusbmax_get(chip);
	vchg_loop = get_prop_vchg_loop(chip);
	if (is_vin_min_detected) {
		cur_jiffies = jiffies;
		time_since_last_update_ms =
			(cur_jiffies - aicl_timer.last_do_jiffies) * MSEC_PER_SEC / HZ;
		aicl_timer.total_time_ms += time_since_last_update_ms;
		aicl_timer.last_do_jiffies = cur_jiffies;

		if (aicl_timer.total_time_ms >= VIN_MIN_DETECT_DURATION_MS) {
			is_vin_min_detected = 0;
			rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
			if (rc)
				pr_err("AICL dec: error reading USBIN channel = %d, rc = %d\n",
							USBIN, rc);
			usbin = (int)result.physical;
			pr_info("AICL: is_vin_min_detected=%d, total_time_ms=%ld, usb_ma=%d, "
					"usbin=%d\n",
					is_vin_min_detected, aicl_timer.total_time_ms, usb_ma, usbin);

			if (htc_battery_is_support_qc20() &&
					usbin > QC20_9_VOLT_CHECK &&
					usb_target_ma != USB_MA_1600 &&
					!is_usb_target_ma_changed_by_qc20) {
				usb_target_ma = USB_MA_1600;
				is_usb_target_ma_changed_by_qc20 = true;
				qpnp_chg_set_appropriate_battery_current(chip);
				qpnp_chg_vinmin_set(chip, QC20_VIN_MIN_MV);
			}

			aicl_timer.total_time_ms = 0;

			if (usb_ma >= usb_target_ma) {
				is_aicl_worker_enabled = false;
				pr_info("AICL: finish! usb_target_ma=%d, aicl_worker=%d\n",
						usb_target_ma, is_aicl_worker_enabled);
				if (!is_usb_target_ma_changed_by_qc20) { /* Not QC2.0 charger */
					if (the_chip->ext_ovpfet_gpio) { /* Check ovpfet charge */
						schedule_delayed_work(&the_chip->ovpfet_resistance_check_work,
							msecs_to_jiffies(OVPFET_R_CHECK_WAIT_MS));
					}
				}

				return;
			}
		}
	}

	/* AICL: leave! because cable is out */
	if ((!qpnp_chg_is_usb_chg_plugged_in(chip))
			&& (!qpnp_chg_is_dc_chg_plugged_in(chip))) {
		aicl_timer.total_time_ms = 0;
		is_aicl_worker_enabled = false;
		qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		pr_info("AICL: exit because cable is out! USB=%d, DC=%d, aicl_worker=%d\n",
				qpnp_chg_is_usb_chg_plugged_in(chip), qpnp_chg_is_dc_chg_plugged_in(chip),
				is_aicl_worker_enabled);
		return;
	}

	/* AICL: decrease usb_current */
	if (usb_target_ma > USB_MA_2) {
		if (vchg_loop
				&& (usb_ma > usb_wall_threshold_ma)
				&& is_vin_min_detected && (!pwrsrc_disabled)) {
			rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
			if (rc)
				pr_err("AICL dec: error reading USBIN channel = %d, rc = %d\n",
							USBIN, rc);
			usbin = (int)result.physical;
			decrease_usb_ma_value(&usb_ma);
			/* end AICL here */
			__pm8941_charger_vbus_draw(usb_ma);
			pr_info("AICL dec: usb_now=%d, usb_target=%d, usbin=%d\n",
				usb_ma, usb_target_ma, usbin);
			usb_target_ma = usb_ma;
			/* For workaround power bank case */
			pm_chg_disable_pwrsrc(chip, 1, PWRSRC_DISABLED_BIT_AICL);
			msleep(350);
			pm_chg_disable_pwrsrc(chip, 0, PWRSRC_DISABLED_BIT_AICL);
			/* Allow retry AICL if usb_ma is only 500 mA, totally retry twice in 10 minutes */
			if (usb_ma == USB_MA_500
					&& chip->retry_aicl_cnt < RETRY_AICL_TOT_COUNT) {
				chip->retry_aicl_cnt++;
				retry_aicl_timer.total_time_ms = 0;
				retry_aicl_timer.last_do_jiffies = jiffies;
				pr_info("AICL: retry_aicl_cnt=%d\n", chip->retry_aicl_cnt);
			} else
				chip->retry_aicl_cnt = 0;
		}
	}

	/* AICL: increase usb_current */
	if ((!vchg_loop) && (usb_target_ma > USB_MA_2)
			&& (!is_vin_min_detected) && (!pwrsrc_disabled)) {
		/* only increase iusb_max if vin loop not active */
		if (usb_ma < usb_target_ma) {
			increase_usb_ma_value(&usb_ma);
			__pm8941_charger_vbus_draw(usb_ma);
			is_vin_min_detected = 1;
			is_aicl_worker_enabled = true;
			aicl_timer.total_time_ms = 0;
			aicl_timer.last_do_jiffies = jiffies;
			pr_info("AICL inc: usb_now=%d, usb_target=%d,"
					" is_vin_min_detected=%d, duration=%ld\n",
					usb_ma, usb_target_ma,
					is_vin_min_detected, aicl_timer.total_time_ms);
		} else {
			usb_target_ma = usb_ma;
		}
	}

	/* AICL: leave! no need to do AICL */
	if (!is_vin_min_detected) {
		is_aicl_worker_enabled = false;
		pr_info("AICL: exit because trigger condition isn't reach!usb_target_ma=%d, "
				"usb_ma=%d, vchg_loop=%d, aicl_worker=%d\n",
			usb_target_ma, usb_ma, vchg_loop, is_aicl_worker_enabled);
		/* Allow retry AICL if usb_ma is only 500 mA, totally retry twice in 10 minutes */
		if (usb_target_ma > usb_wall_threshold_ma
				&& usb_ma == USB_MA_500
				&& chip->retry_aicl_cnt < RETRY_AICL_TOT_COUNT) {
			chip->retry_aicl_cnt++;
			retry_aicl_timer.total_time_ms = 0;
			retry_aicl_timer.last_do_jiffies = jiffies;
			pr_info("AICL: retry_aicl_cnt=%d\n", chip->retry_aicl_cnt);
		} else
			chip->retry_aicl_cnt = 0;
		return;
	}
	queue_delayed_work(chip->aicl_check_wq, &chip->aicl_check_work,
		      msecs_to_jiffies(AICL_CHECK_WAIT_PERIOD_MS));
}

int htc_battery_is_support_qc20(void)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return 0;
	}

	if (the_chip->is_qc20_chg_enabled &&
			the_chip->qc20_ibatmax > 0 &&
			the_chip->qc20_ibatsafe > 0)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(htc_battery_is_support_qc20);

extern int htc_get_usb_charger_type(void);
int htc_battery_check_cable_type_from_usb(void)
{
	int cabletype = 0;

	cabletype = htc_get_usb_charger_type();
	pr_info("[BATT] %s, cabletype=%d", __func__, cabletype);

	return cabletype;
}
EXPORT_SYMBOL(htc_battery_check_cable_type_from_usb);

static void handle_usb_present_change(struct qpnp_chg_chip *chip,
				int usb_present)
{
	int rc = 0;

	if (chip->usb_present ^ usb_present) {
		chip->usb_present = usb_present;
		/* to clear sw eoc flag once plug in/out cable*/
		eoc_count_by_curr = eoc_count = 0;
		is_ac_safety_timeout = is_ac_safety_timeout_twice = false;
		if (!usb_present) {
			/* Store battery data in Emmc when unplug cable in off mode charging. */
			if(board_mfg_mode() == 5)
				pm8941_bms_store_battery_data_emmc();
			qpnp_chg_usb_suspend_enable(chip, 1);
			if (delayed_work_pending(&chip->resume_vddmax_configure_work))
				__cancel_delayed_work(&chip->resume_vddmax_configure_work);
			if (is_batt_full)
				pm8941_bms_batt_full_fake_ocv();

			vddmax_modify = false;
			chip->chg_done = false;
			chip->prev_usb_max_ma = -EINVAL;
			chip->delta_vddmax_mv = 0;
			qpnp_chg_set_appropriate_vddmax(chip);
			chip->retry_aicl_cnt = 0;
			hsml_target_ma = 0;
			usb_target_ma = 0;
			is_aicl_worker_enabled = false;
			is_vin_min_detected = 0;
			is_batt_full = false;
			is_batt_full_eoc_stop = false;
			is_usb_target_ma_changed_by_qc20 = false;
			qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
			if (chip->enable_qct_adjust_vddmax
					&& delayed_work_pending(&chip->readjust_vddmax_configure_work))
				cancel_delayed_work_sync(&chip->readjust_vddmax_configure_work);
			pr_info("Set vbatdet=%d after cable out\n",
								QPNP_CHG_VBATDET_MAX_MV);
			rc = qpnp_chg_vbatdet_set(chip, QPNP_CHG_VBATDET_MAX_MV);
			if (rc)
				pr_err("Failed to set vbatdet=%d rc=%d\n",
								QPNP_CHG_VBATDET_MAX_MV, rc);
		} else {
			pm_stay_awake(chip->dev);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		}
	}

	if (usb_present
			&& (usb_target_ma > usb_wall_threshold_ma)) {
		pr_info("AICL: aicl_check_worker triggered\n");

		queue_delayed_work(chip->aicl_check_wq, &chip->aicl_check_work,
			msecs_to_jiffies(AICL_CHECK_WAIT_PERIOD_MS));
	}

	if(chip->ext_ovpfet_gpio && !usb_present)
		disable_ovpfet_work(chip);

}

int pm8941_set_pwrsrc_and_charger_enable(enum htc_power_source_type src,
		bool chg_enable, bool pwrsrc_enable)
{
	int mA = 0;
	int rc = 0;
	static int pre_pwr_src;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	pr_info("src=%d, pre_pwr_src=%d, chg_enable=%d, pwrsrc_enable=%d, "
			"is_vin_min_detected=%d, usb_target_ma=%d\n",
				src, pre_pwr_src, chg_enable, pwrsrc_enable,
				is_vin_min_detected, usb_target_ma);

	if (src > HTC_PWR_SOURCE_TYPE_BATT && !vddmax_modify) {
		/* Down VDDMAX to vbatt + 100mV for display flicker issue */
		decrease_vddmax_configure_work();

		/* Recover VDDMAX setting after 2secs */
		schedule_delayed_work(&the_chip->resume_vddmax_configure_work,
			msecs_to_jiffies(RESUME_VDDMAX_WORK_MS));

		/* Readjust VDDMAX setting after 30secs if necessary */
		if (the_chip->enable_qct_adjust_vddmax
				&& !delayed_work_pending(&the_chip->readjust_vddmax_configure_work))
			schedule_delayed_work(&the_chip->readjust_vddmax_configure_work,
				msecs_to_jiffies(READJUST_VDDMAX_WORK_MS));
		vddmax_modify = true;
	}

	if (get_kernel_flag() & KERNEL_FLAG_ENABLE_FAST_CHARGE)
		src = htc_fake_charger_for_testing(src);

	pwr_src = src;

	if(src > HTC_PWR_SOURCE_TYPE_BATT)
		the_chip->charging_disabled = false;
	else
		the_chip->charging_disabled = true;

	/* for pm8941 ext usb_in charger */
	if(the_chip->ext_usb)
	{
		return rc;
	}

	/* STEP0: set pwrsrc enable/disable */
	pm_chg_disable_pwrsrc(the_chip, !pwrsrc_enable, PWRSRC_DISABLED_BIT_KDRV);

	/* STEP1: set BATFET */
	pm_chg_disable_auto_enable(the_chip, !chg_enable,
								BATT_CHG_DISABLED_BIT_KDRV);

	/* STEP2: set vbus draw */
	switch (src) {
	case HTC_PWR_SOURCE_TYPE_BATT:
		mA = USB_MA_2;
		break;
	case HTC_PWR_SOURCE_TYPE_WIRELESS:
		if (qpnp_chg_is_dc_chg_plugged_in(the_chip)) {
			pr_info("Wireless charger is from DC_IN\n");
			mA = USB_MA_1100;
		} else
			mA = USB_MA_500;
		break;
	case HTC_PWR_SOURCE_TYPE_DETECTING:
	case HTC_PWR_SOURCE_TYPE_UNKNOWN_USB:
	case HTC_PWR_SOURCE_TYPE_USB:
		if (force_fast_charge)
			mA = USB_MA_1100;
		else
			mA = USB_MA_500;
		break;
	case HTC_PWR_SOURCE_TYPE_AC:
	case HTC_PWR_SOURCE_TYPE_9VAC:
	case HTC_PWR_SOURCE_TYPE_MHL_AC:
		if (the_chip->is_pm8921_aicl_enabled &&
				!(get_kernel_flag() & KERNEL_FLAG_ENABLE_FAST_CHARGE))
			/* AICL: target of iusb_current is 1500 mA */
			mA = aicl_target_ma;
		else
			mA = USB_MA_1100;
		break;
	default:
		mA = USB_MA_2;
		break;
	}

#ifdef CONFIG_ARCH_MSM8974
	/*Do not support charging functionality for pm8941 1.0/2.0v per QCT's comment*/
	if (pmic_rev < 0x3) {
		mA = USB_MA_2;
		pr_warn("Don't support charging function for old pm8941:0x%x rev.\n", pmic_rev);
	}
#endif
	/* FIXME: Need consider wireless charging source */
	if (pre_pwr_src != src) {
		if (the_chip->regulate_vin_min_thr_mv
				&& the_chip->lower_vin_min)
			adjust_chg_vin_min(the_chip, 0);
	}
	pre_pwr_src = src;

	pr_info("qpnp_chg_iusbmax_set :%dmA\n",mA);
	_pm8941_charger_vbus_draw(mA);

	if (the_chip->ext_ovpfet_gpio && HTC_PWR_SOURCE_TYPE_AC == src) {
		if (delayed_work_pending(&the_chip->ovpfet_resistance_check_work))
			disable_ovpfet_work(the_chip);

		/* For enable QC2.0/AICL project, check external ovpfet after finishing QC2.0/AICL detect */
		if (!htc_battery_is_support_qc20() && !the_chip->is_pm8921_aicl_enabled) {
			schedule_delayed_work(&the_chip->ovpfet_resistance_check_work,
				msecs_to_jiffies(OVPFET_R_CHECK_WAIT_MS));
		}
	}

	if (HTC_PWR_SOURCE_TYPE_BATT == src)
		handle_usb_present_change(the_chip, 0);
	else
		handle_usb_present_change(the_chip, 1);

	return rc;
}
int pm8941_charger_enable(bool enable)
{
	int rc = 0;

	/* smb349_enable_charging*/
	/*if(the_chip->ext_usb)
	{
		if(the_chip->ext_usb->ichg->set_charger_enable)
		{
			rc = the_chip->ext_usb->ichg->set_charger_enable(enable);
			return rc;
		}
	}*/

	if (the_chip) {
		enable = !!enable;
		rc = pm_chg_disable_auto_enable(the_chip, !enable, BATT_CHG_DISABLED_BIT_KDRV);
	} else {
		pr_err("called before init\n");
		rc = -EINVAL;
	}

	return rc;
}

/* htc charger interface + */
int pm8941_pwrsrc_enable(bool enable)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/*if(the_chip->ext_usb)
	{
		int ret = 0;

		if(the_chip->ext_usb->ichg->set_pwrsrc_enable)
			ret = the_chip->ext_usb->ichg->set_pwrsrc_enable(enable);

		bms_notify_check(the_chip);
		return ret;
	}*/

	return pm_chg_disable_pwrsrc(the_chip, !enable, PWRSRC_DISABLED_BIT_KDRV);
}

int pm8941_set_chg_iusbmax(int val)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return qpnp_chg_iusbmax_set(the_chip, val);
}

int pm8941_set_chg_vin_min(int val)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return qpnp_chg_vinmin_set(the_chip, val);
}

int pm8941_get_batt_temperature(int *result)
{
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	*result = get_prop_batt_temp(the_chip);
	return 0;
}

int pm8941_get_batt_voltage(int *result)
{
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	*result = (get_prop_battery_voltage_now(the_chip) / 1000);
	return 0;
}

int pm8941_batt_warm_and_cool_recheck(void)
{
	bool bat_warm = 0, bat_cool = 0, adc_notify_fail = false;
	int temp = 0;
	struct qpnp_chg_chip *chip = the_chip;

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	temp = get_prop_batt_temp(chip);

	if(temp > chip->warm_bat_decidegc && !chip->bat_is_warm) {
		/* Normal to warm */
		pr_info("[Normal to warm]batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm);
		bat_warm = true;
		bat_cool = false;
		adc_notify_fail = true;

	} else if(temp < chip->cool_bat_decidegc && !chip->bat_is_cool) {
		/* Normal to cool */
		pr_info("[Normal to cool]batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm);
		bat_warm = false;
		bat_cool = true;
		adc_notify_fail = true;

	} else if(temp < (chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC) && chip->bat_is_warm) {
		/* Warm to normal */
		pr_info("[Warm to normal]batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm);
		bat_warm = false;
		bat_cool = false;
		adc_notify_fail = true;

	} else if (temp > (chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC) && chip->bat_is_cool) {
		/* Cool to normal */
		pr_info("[Cool to normal]batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm);
		bat_warm = false;
		bat_cool = false;
		adc_notify_fail = true;
	}

	if (adc_notify_fail) {
		chip->bat_is_cool = bat_cool;
		chip->bat_is_warm = bat_warm;

		/**
		 * set appropriate voltages and currents.
		 *
		 * Note that when the battery is hot or cold, the charger
		 * driver will not resume with SoC. Only vbatdet is used to
		 * determine resume of charging.
		 */
		qpnp_chg_set_appropriate_vddmax(chip);
		qpnp_chg_set_appropriate_battery_current(chip);
		qpnp_chg_set_appropriate_vbatdet(chip);
		htc_gauge_event_notify(HTC_GAUGE_EVENT_TEMP_ZONE_CHANGE);
		if (chip->ext_ovpfet_gpio && is_ac_online() &&
				!chip->bat_is_cool && !chip->bat_is_warm) {
			if (!delayed_work_pending(&chip->ovpfet_resistance_check_work)) {
				pr_info("re-enable ovpfet_resistance_check_work.\n");
				schedule_delayed_work(&chip->ovpfet_resistance_check_work,
							msecs_to_jiffies(OVPFET_R_CHECK_WAIT_MS));
			}
		}
		pr_info("batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d, bat_cool:%d, bat_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm, bat_cool, bat_warm);
	}

	return 0;
}

int pm8941_is_batt_temp_fault_disable_chg(int *result)
{
	int batt_temp_status, vbat_mv, is_vbatt_over_vddmax;
	int is_cold = 0, is_hot = 0;
	int is_warm = 0;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	batt_temp_status = get_prop_batt_health(the_chip);

	vbat_mv = get_prop_battery_voltage_now(the_chip) / 1000;

	if (batt_temp_status == POWER_SUPPLY_HEALTH_OVERHEAT)
		is_hot = 1;
	if (batt_temp_status == POWER_SUPPLY_HEALTH_COLD)
		is_cold = 1;

	pm8941_batt_warm_and_cool_recheck();

	is_warm = the_chip->bat_is_warm;

	if(vbat_mv >= the_chip->warm_bat_mv)
		is_vbatt_over_vddmax = true;
	else
		is_vbatt_over_vddmax = false;

	pr_debug("is_cold=%d, is_hot=%d, is_warm=%d, is_vbatt_over_vddmax=%d, warm_bat_mv:%d\n",
			is_cold, is_hot, is_warm, is_vbatt_over_vddmax, the_chip->warm_bat_mv);
	if ((is_cold || is_hot || (is_warm && is_vbatt_over_vddmax)) &&
			!flag_keep_charge_on && !flag_pa_recharge)
		*result = 1;
	else
		*result = 0;

	return 0;
}

int pm8941_is_batt_temperature_fault(int *result)
{
	int is_cold = 0, is_warm = 0;
	int batt_temp_status;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	batt_temp_status = get_prop_batt_health(the_chip);

	if (batt_temp_status == POWER_SUPPLY_HEALTH_COLD || the_chip->bat_is_cool)
		is_cold = 1;

	is_warm = the_chip->bat_is_warm;

	pr_debug("is_cold=%d,is_warm=%d\n", is_cold, is_warm);
	if (is_cold || is_warm)
		*result = 1;
	else
		*result = 0;
	return 0;
}

int pm8941_get_battery_status(void)
{
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	return get_prop_batt_status(the_chip);
}

int pm8941_get_batt_present(void)
{
	if (!the_chip) {
		pr_warn("called before init\n");
		return -EINVAL;
	}

	return get_prop_batt_present(the_chip);
}

int pm8941_get_charge_type(void)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return  get_prop_charge_type(the_chip);
}

int pm8941_get_chg_usb_iusbmax(void)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return (qpnp_chg_usb_iusbmax_get(the_chip)*1000);
}

int pm8941_get_chg_vinmin(void)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return (qpnp_chg_vinmin_get(the_chip)*1000);
}

int pm8941_get_input_voltage_regulation(void)
{
	if (!the_chip) {
		pr_err("%s: called before init\n", __func__);
		return -EINVAL;
	}
	return get_prop_vchg_loop(the_chip);
}

int pm8941_get_charging_source(int *result)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/*if(the_chip->ext_usb)
	{
		if(the_chip->ext_usb->ichg->get_charging_source)
			the_chip->ext_usb->ichg->get_charging_source(result);
		return 0;
	}*/

	*result = pwr_src;

	return 0;
}

int pm8941_get_charging_enabled(int *result)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/*if(the_chip->ext_usb)
	{
		if(the_chip->ext_usb->ichg->get_charging_enabled)
			return the_chip->ext_usb->ichg->get_charging_enabled(result);
	}*/

	if (get_prop_batt_status(the_chip) == POWER_SUPPLY_STATUS_CHARGING)
		return pm8941_get_charging_source(result);
	else
		*result = HTC_PWR_SOURCE_TYPE_BATT;
	return 0;
}

static void update_ovp_uvp_state(int ov, int v, int uv)
{
	if ( ov && !v && !uv) {
		if (!ovp) {
			ovp = 1;
			pr_info("OVP: 0 -> 1, USB_Valid: %d\n", v);
			htc_charger_event_notify(HTC_CHARGER_EVENT_OVP);
		}
		if (uvp) {
			uvp = 0;
			pr_debug("UVP: 1 -> 0, USB_Valid: %d\n", v);
		}
	} else if ( !ov && !v && uv) {
		if (ovp) {
			ovp = 0;
			pr_info("OVP: 1 -> 0, USB_Valid: %d\n", v);
			htc_charger_event_notify(HTC_CHARGER_EVENT_OVP_RESOLVE);
		}
		if (!uvp) {
			uvp = 1;
			pr_debug("UVP: 0 -> 1, USB_Valid: %d\n", v);
		}
	} else {
		if (ovp) {
			ovp = 0;
			pr_info("OVP: 1 -> 0, USB_Valid: %d\n", v);
			htc_charger_event_notify(HTC_CHARGER_EVENT_OVP_RESOLVE);
		}
		if (uvp) {
			uvp = 0;
			pr_debug("UVP: 1 -> 0, USB_Valid: %d\n", v);
		}
	}
	pr_info("ovp=%d, uvp=%d [%d,%d,%d]\n", ovp, uvp, ov, v, uv);
}

int pm8941_is_charger_ovp(int* result)
{
	int ov = false, uv = false, v = false;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	get_prop_usb_valid_status(the_chip, &ov, &v, &uv);

	update_ovp_uvp_state(ov, v, uv);
	*result = ovp;

	/*if(the_chip->ext_usb)
	{
		if(the_chip->ext_usb->ichg->is_ovp)
			the_chip->ext_usb->ichg->is_ovp(result);
	}*/

	return 0;
}

static int64_t read_battery_id(struct qpnp_chg_chip *chip)
{
	int rc;
	struct qpnp_vadc_result result;

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX2_BAT_ID, &result);
	if (rc) {
		pr_err("error reading batt id channel = %d, rc = %d\n",
					LR_MUX2_BAT_ID, rc);
		return rc;
	}

	pr_debug("batt_id phy = %lld meas = 0x%llx\n", result.physical,
						result.measurement);
	pr_debug("raw_code = 0x%x\n", result.adc_code);

	return result.physical;
}

static int get_chgr_reg(void *data, u64 *val)
{
	int addr = (int)data;
	int rc;
	u8 chgr_sts;

	rc = qpnp_chg_read(the_chip, &chgr_sts,
			the_chip->chgr_base + addr, 1);
	if (rc) {
		pr_err("failed to read chgr_base register sts %d\n", rc);
		return -EAGAIN;
	}
	pr_debug("addr:0x%X, val:0x%X\n", (the_chip->chgr_base + addr), chgr_sts);
	*val = chgr_sts;
	return 0;
}

static int get_bat_if_reg(void *data, u64 *val)
{
	int addr = (int)data;
	int rc;
	u8 bat_if_sts;

	rc = qpnp_chg_read(the_chip, &bat_if_sts,
			the_chip->bat_if_base + addr, 1);
	if (rc) {
		pr_err("failed to read bat_if_base register sts %d\n", rc);
		return -EAGAIN;
	}
	pr_debug("addr:0x%X, val:0x%X\n", (the_chip->bat_if_base + addr), bat_if_sts);
	*val = bat_if_sts;
	return 0;
}

static int get_usb_chgpth_reg(void *data, u64 *val)
{
	int addr = (int)data;
	int rc;
	u8 chgpth_sts;

	rc = qpnp_chg_read(the_chip, &chgpth_sts,
			the_chip->usb_chgpth_base + addr, 1);
	if (rc) {
		pr_err("failed to read chgpth_sts register sts %d\n", rc);
		return -EAGAIN;
	}
	pr_debug("addr:0x%X, val:0x%X\n", (the_chip->usb_chgpth_base + addr), chgpth_sts);
	*val = chgpth_sts;
	return 0;
}

static int get_misc_reg(void *data, u64 *val)
{
	int addr = (int)data;
	int rc;
	u8 misc_sts;

	rc = qpnp_chg_read(the_chip, &misc_sts,
			the_chip->misc_base + addr, 1);
	if (rc) {
		pr_err("failed to read misc_sts register sts %d\n", rc);
		return -EAGAIN;
	}
	pr_debug("addr:0x%X, val:0x%X\n", (the_chip->misc_base + addr), misc_sts);
	*val = misc_sts;
	return 0;
}


static int get_dc_chgpth_reg(void *data, u64 *val)
{
	int addr = (int)data;
	int rc;
	u8 dc_chgpth_sts;

	rc = qpnp_chg_read(the_chip, &dc_chgpth_sts,
			the_chip->dc_chgpth_base + addr, 1);
	if (rc) {
		pr_err("failed to read dc_chgpth_sts register sts %d\n", rc);
		return -EAGAIN;
	}
	pr_debug("addr:0x%X, val:0x%X\n", (the_chip->dc_chgpth_base + addr), dc_chgpth_sts);
	*val = dc_chgpth_sts;
	return 0;
}

static void dump_reg(void)
{
	u64 val;
	unsigned int len =0;

	memset(batt_log_buf, 0, sizeof(BATT_LOG_BUF_LEN));
	/* SMBB_CHGR_SMBB_CHGR */
	get_chgr_reg((void *)CHGR_VBAT_STATUS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VBAT_STATUS=0x%llX,", val);
	get_chgr_reg((void *)CHGR_VDD_MAX, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VDD_MAX=0x%llX,", val);
	get_chgr_reg((void *)CHGR_VDD_SAFE, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VDD_SAFE=0x%llX,", val);
	get_chgr_reg((void *)CHGR_IBAT_MAX, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "IBAT_MAX=0x%llX,", val);
	get_chgr_reg((void *)CHGR_IBAT_SAFE, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "IBAT_SAFE=0x%llX,", val);
	get_chgr_reg((void *)CHGR_VIN_MIN, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VIN_MIN=0x%llX,", val);
	get_chgr_reg((void *)CHGR_CHG_CTRL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "CHG_CTRL=0x%llX,", val);
	get_chgr_reg((void *)CHGR_ATC_CTRL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "ATC_CTRL=0x%llX,", val);
	get_chgr_reg((void *)CHGR_IBAT_TERM_CHGR, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "IBAT_TERM_CHGR=0x%llX,", val);
	get_chgr_reg((void *)CHGR_VBAT_DET, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VBAT_DET=0x%llX,", val);
	get_chgr_reg((void *)CHGR_TCHG_MAX_EN, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "TCHG_MAX_EN=0x%llX,", val);
	get_chgr_reg((void *)CHGR_TCHG_MAX, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "TCHG_MAX=0x%llX,", val);
	get_chgr_reg((void *)CHGR_CHG_WDOG_TIME, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "WDOG_TIME=0x%llX,", val);
	get_chgr_reg((void *)CHGR_CHG_WDOG_EN, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "WDOG_EN=0x%llX,", val);
	get_chgr_reg((void *)CHGR_CHG_TEMP_THRESH, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "TEMP_THRESH=0x%llX,", val);
	get_chgr_reg((void *)CHGR_IR_DROP_COMPEN, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "CHGR_IR_DROP=0x%llX,", val);
	get_chgr_reg((void *)CHGR_VDD_MAX_COMPEN, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "VDD_MAX_COMPEN=0x%llX,", val);

	/* SMBB_BAT_IF_SMBB_BAT_IF */
	get_bat_if_reg((void *)CHGR_BAT_IF_PRES_STATUS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "PRES_STATUS=0x%llX,", val);
	get_bat_if_reg((void *)BAT_IF_BAT_TEMP_STATUS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "TEMP_STATUS=0x%llX,", val);
	get_bat_if_reg((void *)BAT_IF_BAT_FET_STATUS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "FET_STATUS=0x%llX,", val);
	get_bat_if_reg((void *)BAT_IF_BPD_CTRL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "BPD_CTRL=0x%llX,", val);
	get_bat_if_reg((void *)BAT_IF_BTC_CTRL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "BTC_CTRL=0x%llX,", val);

	/* SMBB_USB_CHGPTH_SMBB_USB */
	get_usb_chgpth_reg((void *)CHGPTH_USB_CHG_PTH_STS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "USB_CHG_PTH_STS=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_CHG_GONE_INT, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "CHG_GONE_INT=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_STS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "AICL_STS=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_I_AUTO, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "AICL_I_AUTO=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_I_TGT, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "AICL_I_TGT=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_OVP_CTL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "USB_OVP_CTL=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_IUSB_MAX, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "IUSB_MAX=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_SUSP, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "USB_SUSP=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_OTG_CTL, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "OTG_CTL=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_ENUM_TIMER_STOP, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "ENUM_TIMER_STOP=0x%llX,", val);
	get_usb_chgpth_reg((void *)CHGPTH_ENUM_TIMER, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "ENUM_TIMER=0x%llX,", val);

	/* SMBB_DC_CHGPTH_SMBB_DC */
	get_dc_chgpth_reg((void *)CHGPTH_DC_CHG_PTH_STS, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "DC_CHG_PTH_STS=0x%llX,", val);

	/* SMBB_MISC_SMBB_MISC */
	get_misc_reg((void *)CHGR_MISC_BOOT_DONE, &val);
	len += scnprintf(batt_log_buf + len, BATT_LOG_BUF_LEN - len, "BOOT_DONE=0x%llX", val);

/*Log len is larger than log buffer, please decrease log length*/
	if(BATT_LOG_BUF_LEN - len <= 1)
		pr_warn("batt log length maybe out of buffer range!!!");

	pr_info("%s\n", batt_log_buf);
}

static void dump_irq_rt_status(struct qpnp_chg_chip *chip)
{
		unsigned long flags;

		local_irq_save(flags);
		pr_info("[CHGR_INT] %d%d%d%d %d%d%d%d, [BUCK_INT] %d%d%d%d %d%d%d, "
			"[BATIF_INT] %d%d%d%d, [USB_CHGPTH] %d%d%d, [DC_CHGPTH]%d%d, [BOOST_INT]%d%d\n",

		/* SMBB_CHGR_INT_RT_STS 0x1010*/
		irq_read_line(chip->chg_is_done.irq),
		irq_read_line(chip->chg_failed.irq),
		irq_read_line(chip->chg_fastchg.irq),
		irq_read_line(chip->chg_trklchg.irq),
		irq_read_line(chip->chg_state_change.irq),
		irq_read_line(chip->chgwdog.irq),
		irq_read_line(chip->chg_vbatdet_hi.irq),
		irq_read_line(chip->chg_vbatdet_lo.irq),

		/* SMBB_BUCK_INT_RT_STS 0x1110*/
		irq_read_line(chip->buck_vdd_loop.irq),
		irq_read_line(chip->buck_ibat_loop.irq),
		irq_read_line(chip->buck_ichg_loop.irq),
		irq_read_line(chip->vchg_loop.irq),
		irq_read_line(chip->buck_overtemp.irq),
		irq_read_line(chip->buck_vreg_ov.irq),
		irq_read_line(chip->buck_vbat_ov.irq),

		/* SMBB_BAT_IF_INT_RT_STS 0x1210*/
		irq_read_line(chip->vcp_on.irq),
		irq_read_line(chip->batt_fet_on.irq),
		irq_read_line(chip->batt_temp_ok.irq),
		irq_read_line(chip->batt_pres.irq),

		/* SMBB_USB_CHGPTH_INT_RT_STS 0x1310*/
		irq_read_line(chip->chg_gone.irq),
		irq_read_line(chip->usbin_valid.irq),
		irq_read_line(chip->coarse_usb_det.irq),

		/* SMBB_DC_CHGPTH_INT_RT_STS 0x1410*/
		irq_read_line(chip->dcin_valid.irq),
		irq_read_line(chip->coarse_dc_det.irq),

		/* SMBB_BOOST_INT_RT_STS 0x1510*/
		irq_read_line(chip->limit_error.irq),
		irq_read_line(chip->boost_pwr_ok.irq));
		local_irq_restore(flags);
}

int pm8941_get_chgr_fsm_state(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 fsm, reg;

	/* Unsecure PMIC register for chgr peripheral*/
	rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	reg = 0x01;
	rc = qpnp_chg_write(chip, &reg,
				chip->chgr_base + 0xE6, 1);
	if (rc)
		pr_err("failed to unsecure charger fsm rc=%d\n", rc);

	rc = qpnp_chg_read(chip, &fsm,
			chip->chgr_base + CHGR_FSM_STATE, 1);
	if (rc) {
		pr_err("failed to read charger fsm state %d\n", rc);
		return rc;
	}

	return fsm;
}

int pm8941_set_hsml_target_ma(int target_ma)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	pr_info("target_ma= %d\n", target_ma);
	hsml_target_ma = target_ma;

	if((hsml_target_ma != 0) && (pwr_src == HTC_PWR_SOURCE_TYPE_USB)) {
			_pm8941_charger_vbus_draw(hsml_target_ma);
	}

	return 0;
}

/*
 * dump_all - print out all charger and battery status
 *
 */
static void dump_all(int more)
{
	int rc;
	struct qpnp_vadc_result result;
	int vbat_mv, ibat_ma, tbat_deg, soc, id_mv, iusb_ma;
	int health, present, charger_type, status, host_mode;
	int fsm, ac_online, usb_online, dc_online, vin_min;
	int usbin = 0, temp_fault = 0, vchg = 0;
	/*int ichg = 0, vph_pwr = 0, usbin = 0, dcin = 0;*/
	u8 chgr_sts = 0, buck_sts = 0, bat_if_sts = 0;

	vbat_mv = (get_prop_battery_voltage_now(the_chip)/1000);
	pm8941_bms_get_batt_soc(&soc);
	pm8941_bms_get_batt_current(&ibat_ma);
	ibat_ma = ibat_ma/1000;
	/*FIXME to implment FCC func*/
	/*fcc = get_prop_batt_fcc(the_chip);*/
	id_mv = (int)read_battery_id(the_chip)/1000;
	health = get_prop_batt_health(the_chip);
	present = get_prop_batt_present(the_chip);
	charger_type = get_prop_charge_type(the_chip);
	status = get_prop_batt_status(the_chip);
	tbat_deg = get_prop_batt_temp(the_chip)/10;
	if (tbat_deg >= 68)
		pr_warn("battery temperature=%d >= 68\n", tbat_deg);

	chgr_sts = pm_get_chgr_int_rt_sts(the_chip);
	buck_sts = pm_get_buck_int_rt_sts(the_chip);
	bat_if_sts = pm_get_bat_if_int_rt_sts(the_chip);
	fsm = pm8941_get_chgr_fsm_state(the_chip);
	ac_online = is_ac_online();
	usb_online = qpnp_chg_is_usb_chg_plugged_in(the_chip);
	dc_online = qpnp_chg_is_dc_chg_plugged_in(the_chip);
	vin_min = qpnp_chg_vinmin_get(the_chip);
	pm8941_is_batt_temperature_fault(&temp_fault);

	iusb_ma = qpnp_chg_usb_iusbmax_get(the_chip);
	host_mode = qpnp_chg_is_otg_en_set(the_chip);
	rc = qpnp_vadc_read(the_chip->vadc_dev, USBIN, &result);
	if (rc) {
		pr_err("error reading USBIN channel = %d, rc = %d\n",
					USBIN, rc);
	}
	usbin = (int)result.physical;

	rc = qpnp_vadc_read(the_chip->vadc_dev, VCHG_SNS, &result);
	if (rc) {
		pr_err("error reading VCHG_SNS channel = %d, rc = %d\n",
					VCHG_SNS, rc);
	}
	vchg = (int)result.physical;

	/*FIXME need to check how to do the same thing in pm8941*/
	/*rc = pm8xxx_adc_read(CHANNEL_ICHG, &result);
	if (rc) {
		pr_err("error reading i_chg channel = %d, rc = %d\n",
					CHANNEL_ICHG, rc);
	}
	ichg = (int)result.physical;

	rc = pm8xxx_adc_read(CHANNEL_VPH_PWR, &result);
	if (rc) {
		pr_err("error reading vph_pwr channel = %d, rc = %d\n",
					CHANNEL_VPH_PWR, rc);
	}
	vph_pwr = (int)result.physical;

	dcin = get_prop_dcin_uvolts(the_chip);

	vbatdet_low = pm_chg_get_rt_status(the_chip, VBATDET_LOW_IRQ);
	if (the_chip->wlc_tx_gpio)
		is_wlc_remove = gpio_get_value(the_chip->wlc_tx_gpio);*/

	/*FIXME need to add usb_target_ma, is_cable_remove flags*/
	printk(KERN_INFO "[BATT][CHG] V=%d mV, I=%d mA, T=%d C, SoC=%d%%,id=%d mV,"
			"H=%d,P=%d,CHG=%d,S=%d,FSM=%d,CHGR_STS=%X,BUCK_STS=%X,BATIF_STS=%X,"
			"AC=%d,USB=%d,DC=%d,iusb_ma=%d,OVP=%d,UVP=%d,TM=%d,usbin=%d,vchg=%d,"
			"eoc_count/by_curr=%d/%d,is_ac_ST=%d,batfet_dis=0x%x,pwrsrc_dis=0x%x,is_full=%d,"
			"temp_fault=%d,bat_is_warm/cool=%d/%d,flag=%d%d%d,vin_min=%d,host=%d,"
			"hsml_ma=%d\n",
			vbat_mv, ibat_ma, tbat_deg, soc, id_mv,
			health, present, charger_type, status, fsm, chgr_sts, buck_sts, bat_if_sts,
			ac_online, usb_online, dc_online, iusb_ma, ovp, uvp, thermal_mitigation, usbin, vchg,
			eoc_count, eoc_count_by_curr, is_ac_safety_timeout,
			batt_charging_disabled, pwrsrc_disabled, is_batt_full, temp_fault,
			the_chip->bat_is_warm, the_chip->bat_is_cool,
			flag_keep_charge_on, flag_pa_recharge, the_chip->charging_disabled,
			vin_min, host_mode, hsml_target_ma);
			/*vbatdet_low,
			pm8xxx_adc_btm_is_warm(), pm8xxx_adc_btm_is_cool(), ichg, vph_pwr,
			usbin, dcin, pwrsrc_under_rating, usbin_critical_low_cnt,
			the_chip->disable_reverse_boost_check, test_power_monitor,
			flag_disable_wakelock, hsml_target_ma);*/
	/* check any abnormal case that we need to dump more */
	dump_irq_rt_status(the_chip);
	dump_reg();
	pm8941_bms_dump_all();
	/*is_cable_remove = false;*/
}

inline int pm8941_dump_all(void)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}
	dump_all(0);

	return 0;
}

int pm8941_is_batt_full(int *result)
{
        if (!the_chip) {
                pr_err("called before init\n");
                return -EINVAL;
        }
        *result = is_batt_full;
        return 0;
}

int pm8941_is_batt_full_eoc_stop(int *result)
{
       if (!the_chip) {
               pr_err("called before init\n");
               return -EINVAL;
       }
       *result = is_batt_full_eoc_stop;
       return 0;
}

int pm8941_charger_get_attr_text(char *buf, int size)
{
	int rc;
	struct qpnp_vadc_result result;
	int len = 0;
	u64 val = 0;
	unsigned long flags;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	local_irq_save(flags);
	len += scnprintf(buf + len, size - len,
			"FSM: %d;\n"
			"CHG_DONE_IRQ: %d;\n"
			"CHG_FAIL_IRQ: %d;\n"
			"FAST_CHG_IRQ: %d;\n"
			"TRKL_CHG_IRQ: %d;\n"
			"STATE_CHANGE_IRQ: %d;\n"
			"CHGWDOG_IRQ: %d;\n"
			"VBAT_DET_HI_IRQ: %d;\n"
			"VBAT_DET_LO_IRQ: %d;\n"
			"VDD_LOOP_IRQ: %d;\n"
			"IBAT_LOOP_IRQ: %d;\n"
			"ICHG_LOOP_IRQ: %d;\n"
			"VCHG_LOOP_IRQ: %d;\n"
			"OVERTEMP_LOOP_IRQ: %d;\n"
			"VREG_OV_LOOP_IRQ: %d;\n"
			"VBAT_OV_LOOP_IRQ: %d;\n"
			"VCP_ON_IRQ: %d;\n"
			"BAT_FET_ON_IRQ: %d;\n"
			"BAT_TEMP_OK_IRQ: %d;\n"
			"BAT_PRES_IRQ: %d;\n"
			"CHG_GONE_IRQ: %d;\n"
			"USBIN_VALID_IRQ: %d;\n"
			"COARSE_DET_USB_IRQ: %d;\n"
			"DCIN_VALID_IRQ: %d;\n"
			"COARSE_DET_DC_IRQ: %d;\n"
			"LIMIT_ERROR_IRQ: %d;\n"
			"BOOST_PWR_OK_IRQ: %d;\n",

			pm8941_get_chgr_fsm_state(the_chip),
			/* SMBB_CHGR_INT_RT_STS 0x1010*/
			irq_read_line(the_chip->chg_is_done.irq),
			irq_read_line(the_chip->chg_failed.irq),
			irq_read_line(the_chip->chg_fastchg.irq),
			irq_read_line(the_chip->chg_trklchg.irq),
			irq_read_line(the_chip->chg_state_change.irq),
			irq_read_line(the_chip->chgwdog.irq),
			irq_read_line(the_chip->chg_vbatdet_hi.irq),
			irq_read_line(the_chip->chg_vbatdet_lo.irq),

			/* SMBB_BUCK_INT_RT_STS 0x1110*/
			irq_read_line(the_chip->buck_vdd_loop.irq),
			irq_read_line(the_chip->buck_ibat_loop.irq),
			irq_read_line(the_chip->buck_ichg_loop.irq),
			irq_read_line(the_chip->vchg_loop.irq),
			irq_read_line(the_chip->buck_overtemp.irq),
			irq_read_line(the_chip->buck_vreg_ov.irq),
			irq_read_line(the_chip->buck_vbat_ov.irq),

			/* SMBB_BAT_IF_INT_RT_STS 0x1210*/
			irq_read_line(the_chip->vcp_on.irq),
			irq_read_line(the_chip->batt_fet_on.irq),
			irq_read_line(the_chip->batt_temp_ok.irq),
			irq_read_line(the_chip->batt_pres.irq),

			/* SMBB_USB_CHGPTH_INT_RT_STS 0x1310*/
			irq_read_line(the_chip->chg_gone.irq),
			irq_read_line(the_chip->usbin_valid.irq),
			irq_read_line(the_chip->coarse_usb_det.irq),

			/* SMBB_DC_CHGPTH_INT_RT_STS 0x1410*/
			irq_read_line(the_chip->dcin_valid.irq),
			irq_read_line(the_chip->coarse_dc_det.irq),

			/* SMBB_BOOST_INT_RT_STS 0x1510*/
			irq_read_line(the_chip->limit_error.irq),
			irq_read_line(the_chip->boost_pwr_ok.irq));
	local_irq_restore(flags);

	rc = qpnp_vadc_read(the_chip->vadc_dev, USBIN, &result);
	if (rc) {
		pr_err("error reading USBIN channel = %d, rc = %d\n",
					USBIN, rc);
	}
	len += scnprintf(buf + len, size - len,
			"USBIN(uV): %d;\n", (int)result.physical);

	len += scnprintf(buf + len, size - len,
			"AC_SAFETY_TIMEOUT(bool): %d;\n", (int)is_ac_safety_timeout);

	len += scnprintf(buf + len, size - len,
			"AC_SAFETY_TIMEOUT2(bool): %d;\n", (int)is_ac_safety_timeout_twice);

	len += scnprintf(buf + len, size - len,
			"eoc_count/by_curr(int): %d/%d;\n", eoc_count, eoc_count_by_curr);

	len += scnprintf(buf + len, size - len,
			"mitigation_level(int): %d;\n", thermal_mitigation);

	/* SMBB_CHGR_SMBB_CHGR */
	get_chgr_reg((void *)CHGR_VDD_MAX, &val);
	len += scnprintf(buf + len, size - len, "VDD_MAX: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_VDD_SAFE, &val);
	len += scnprintf(buf + len, size - len, "VDD_SAFE: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_IBAT_MAX, &val);
	len += scnprintf(buf + len, size - len, "IBAT_MAX: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_IBAT_SAFE, &val);
	len += scnprintf(buf + len, size - len, "IBAT_SAFE: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_VIN_MIN, &val);
	len += scnprintf(buf + len, size - len, "VIN_MIN: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_CHG_CTRL, &val);
	len += scnprintf(buf + len, size - len, "CHG_CTRL: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_ATC_CTRL, &val);
	len += scnprintf(buf + len, size - len, "ATC_CTRL: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_IBAT_TERM_CHGR, &val);
	len += scnprintf(buf + len, size - len, "IBAT_TERM_CHGR: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_VBAT_DET, &val);
	len += scnprintf(buf + len, size - len, "VBAT_DET: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_TCHG_MAX_EN, &val);
	len += scnprintf(buf + len, size - len, "TCHG_MAX_EN: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_TCHG_MAX, &val);
	len += scnprintf(buf + len, size - len, "TCHG_MAX: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_CHG_WDOG_TIME, &val);
	len += scnprintf(buf + len, size - len, "WDOG_TIME: 0x%llX;\n", val);
	get_chgr_reg((void *)CHGR_CHG_WDOG_EN, &val);
	len += scnprintf(buf + len, size - len, "WDOG_EN: 0x%llX;\n", val);
	/* SMBB_BAT_IF_SMBB_BAT_IF */
	get_bat_if_reg((void *)CHGR_BAT_IF_PRES_STATUS, &val);
	len += scnprintf(buf + len, size - len, "PRES_STATUS: 0x%llX;\n", val);
	get_bat_if_reg((void *)BAT_IF_BAT_TEMP_STATUS, &val);
	len += scnprintf(buf + len, size - len, "TEMP_STATUS: 0x%llX;\n", val);
	get_bat_if_reg((void *)BAT_IF_BAT_FET_STATUS, &val);
	len += scnprintf(buf + len, size - len, "FET_STATUS: 0x%llX;\n", val);
	get_bat_if_reg((void *)BAT_IF_BPD_CTRL, &val);
	len += scnprintf(buf + len, size - len, "BPD_CTRL: 0x%llX;\n", val);
	get_bat_if_reg((void *)BAT_IF_BTC_CTRL, &val);
	len += scnprintf(buf + len, size - len, "BTC_CTRL: 0x%llX;\n", val);

	/* SMBB_USB_CHGPTH_SMBB_USB */
	get_usb_chgpth_reg((void *)CHGPTH_USB_CHG_PTH_STS, &val);
	len += scnprintf(buf + len, size - len, "USB_CHG_PTH_STS: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_CHG_GONE_INT, &val);
	len += scnprintf(buf + len, size - len, "CHG_GONE_INT: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_STS, &val);
	len += scnprintf(buf + len, size - len, "AICL_STS: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_I_AUTO, &val);
	len += scnprintf(buf + len, size - len, "AICL_I_AUTO: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_AICL_I_TGT, &val);
	len += scnprintf(buf + len, size - len, "AICL_I_TGT: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_OVP_CTL, &val);
	len += scnprintf(buf + len, size - len, "USB_OVP_CTL: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_IUSB_MAX, &val);
	len += scnprintf(buf + len, size - len, "IUSB_MAX: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_SUSP, &val);
	len += scnprintf(buf + len, size - len, "USB_SUSP: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_USB_OTG_CTL, &val);
	len += scnprintf(buf + len, size - len, "OTG_CTL: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_ENUM_TIMER_STOP, &val);
	len += scnprintf(buf + len, size - len, "ENUM_TIMER_STOP: 0x%llX;\n", val);
	get_usb_chgpth_reg((void *)CHGPTH_ENUM_TIMER, &val);
	len += scnprintf(buf + len, size - len, "ENUM_TIMER: 0x%llX;\n", val);

	/* SMBB_MISC_SMBB_MISC */
	get_misc_reg((void *)CHGR_MISC_BOOT_DONE, &val);
	len += scnprintf(buf + len, size - len, "BOOT_DONE: 0x%llX", val);

	return len;
}

/* htc gauge interface - */
int pm8941_gauge_get_attr_text(char *buf, int size)
{
	int len = 0;
	int soc, ibat_ma;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	pm8941_bms_get_batt_soc(&soc);
	pm8941_bms_get_batt_current(&ibat_ma);

	len += scnprintf(buf + len, size - len,
			"SOC(%%): %d;\n"
			"EOC(bool): %d;\n"
			"OVP(bool): %d;\n"
			"UVP(bool): %d;\n"
			"VBAT(uV): %d;\n"
			"IBAT(uA): %d;\n"
			"ID_RAW(uV): %d;\n"
			"BATT_TEMP(deci-celsius): %d;\n"
			"bat_is_warm(bool): %d\n"
			"bat_is_cool(bool): %d\n"
			"BATT_PRESENT(bool): %d;\n"
			"FCC(uAh): %d;\n",
			soc,
			is_batt_full,
			ovp,
			uvp,
			get_prop_battery_voltage_now(the_chip),
			ibat_ma,
			(int)read_battery_id(the_chip),
			get_prop_batt_temp(the_chip),
			the_chip->bat_is_warm,
			the_chip->bat_is_cool,
			get_prop_batt_present(the_chip),
			pm8941_bms_get_fcc()
			/*usb_target_ma*/
			);

	len += pm8941_bms_get_attr_text(buf + len, size - len);

	return len;
}

int pm8941_fake_chg_gone_irq_handler(void)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/* issue a fake chg_gone irq to avoid reverse boost happen during kernel init*/
	qpnp_chg_usb_chg_gone_irq_handler(the_chip->chg_gone.irq, the_chip);

	return 0;
}

int pm8941_fake_usbin_valid_irq_handler(void)
{
	struct qpnp_vadc_result result;
        int rc, usbin = 0, vchg = 0;

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	rc = qpnp_vadc_read(the_chip->vadc_dev, USBIN, &result);
	if (rc) {
		pr_err("error reading USBIN channel = %d, rc = %d\n",
					USBIN, rc);
	}
	usbin = (int)result.physical;

	rc = qpnp_vadc_read(the_chip->vadc_dev, VCHG_SNS, &result);
	if (rc) {
		pr_err("error reading VCHG_SNS channel = %d, rc = %d\n",
					VCHG_SNS, rc);
	}
	vchg = (int)result.physical;

	pr_info("usbin= %dmV, vchg= %dmV\n", usbin, vchg);

	/* issue a fake vbus change to force update */
	qpnp_chg_usb_usbin_valid_irq_handler(the_chip->usbin_valid.irq, the_chip);

	return 0;
}

int pm8941_fake_coarse_det_usb_irq_handler(void)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/* issue a fake coarse_det_usb irq to check ovp status */
	qpnp_chg_coarse_det_usb_irq_handler(the_chip->coarse_usb_det.irq, the_chip);

	return 0;
}

#ifdef CONFIG_DUTY_CYCLE_LIMIT
int pm8941_limit_charge_enable(int chg_limit_reason, int chg_limit_timer_sub_mask, int limit_charge_timer_ma)
{
	pr_info("chg_limit_reason=%d, chg_limit_timer_sub_mask=%d, limit_charge_timer_ma=%d\n",
			chg_limit_reason, chg_limit_timer_sub_mask, limit_charge_timer_ma);

	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	/* NOTE: limit_charge_timer_ma works if it is not 0 and timer_enable is not 0 */
	if (limit_charge_timer_ma != 0 && !!(chg_limit_reason & chg_limit_timer_sub_mask))
		chg_limit_current = limit_charge_timer_ma;
	else {
		if (!!chg_limit_reason)
			chg_limit_current = PM8941_CHG_I_MIN_MA;
		else
			chg_limit_current = 0;
	}

	pr_info("%s:chg_limit_current = %d\n", __func__, chg_limit_current);
	qpnp_chg_set_appropriate_battery_current(the_chip);
	return 0;
}
#else
int pm8941_limit_charge_enable(bool enable)
{
	pr_info("limit_charge=%d\n", enable);
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	if (enable)
		chg_limit_current = PM8941_CHG_I_MIN_MA;
	else
		chg_limit_current = 0;

	qpnp_chg_set_appropriate_battery_current(the_chip);
	return 0;
}
#endif

int pm8941_is_chg_safety_timer_timeout(int *result)
{
	if (!the_chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}
	*result = is_ac_safety_timeout;
	return 0;
}

/**
 * set_thermal_mitigation_level -
 *
 * Internal function to control battery charging current to reduce
 * temperature
 */
static int set_therm_mitigation_level(const char *val, struct kernel_param *kp)
{
	int ret;
	struct qpnp_chg_chip *chip = the_chip;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	}

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	if (!chip->thermal_mitigation) {
		pr_err("no thermal mitigation\n");
		return -EINVAL;
	}

	if (thermal_mitigation < 0
		|| thermal_mitigation >= chip->thermal_levels) {
		pr_err("out of bound level selected\n");
		return -EINVAL;
	}

	pr_info("set mitigation level(%d) current=%d\n",
			thermal_mitigation, chip->thermal_mitigation[thermal_mitigation]);
	qpnp_chg_set_appropriate_battery_current(chip);
	return ret;
}
module_param_call(thermal_mitigation, set_therm_mitigation_level,
					param_get_uint,
					&thermal_mitigation, 0644);
#endif /* !CONFIG_HTC_BATT_8960 */

#define BTC_CONFIG_ENABLED	BIT(7)
#define BTC_COLD		BIT(1)
#define BTC_HOT			BIT(0)
static int
qpnp_chg_bat_if_configure_btc(struct qpnp_chg_chip *chip)
{
	u8 btc_cfg = 0, mask = 0;

	/* Do nothing if battery peripheral not present */
	if (!chip->bat_if_base)
		return 0;

	if ((chip->hot_batt_p == HOT_THD_25_PCT)
			|| (chip->hot_batt_p == HOT_THD_35_PCT)) {
		btc_cfg |= btc_value[chip->hot_batt_p];
		mask |= BTC_HOT;
	}

	if ((chip->cold_batt_p == COLD_THD_70_PCT) ||
			(chip->cold_batt_p == COLD_THD_80_PCT)) {
		btc_cfg |= btc_value[chip->cold_batt_p];
		mask |= BTC_COLD;
	}

	/* Diable or enable temperature protection by kernel flag */
	if (chip->btc_disabled || flag_keep_charge_on || flag_pa_recharge) {
		/* Disable temp protect */
		mask |= BTC_CONFIG_ENABLED;
	} else {
		/* Enable temp protect */
		btc_cfg |= BTC_CONFIG_ENABLED;
		mask |= BTC_CONFIG_ENABLED;
	}

	return qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BTC_CTRL,
			mask, btc_cfg, 1);
}

#define QPNP_CHG_IBATSAFE_MIN_MA		50
#define QPNP_CHG_IBATSAFE_MAX_MA		3250
#define QPNP_CHG_I_STEP_MA		50
#define QPNP_CHG_I_MIN_MA		100
#define QPNP_CHG_I_MASK			0x3F
static int
qpnp_chg_ibatsafe_set(struct qpnp_chg_chip *chip, int safe_current)
{
	u8 temp;

	if (safe_current < QPNP_CHG_IBATSAFE_MIN_MA
			|| safe_current > QPNP_CHG_IBATSAFE_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", safe_current);
		return -EINVAL;
	}

	temp = safe_current / QPNP_CHG_I_STEP_MA;

	pr_info("ibatsafe=%dmA setting %02x\n", safe_current, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_SAFE,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_ITERM_MIN_MA		100
#define QPNP_CHG_ITERM_MAX_MA		250
#define QPNP_CHG_ITERM_STEP_MA		50
#define QPNP_CHG_ITERM_MASK			0x03
static int
qpnp_chg_ibatterm_set(struct qpnp_chg_chip *chip, int term_current)
{
	u8 temp;

	if (term_current < QPNP_CHG_ITERM_MIN_MA
			|| term_current > QPNP_CHG_ITERM_MAX_MA) {
		pr_warn("out of ibatterm range mA=%d, keep default value\n", term_current);

		temp = 0x00;
		return qpnp_chg_masked_write(chip,
				chip->chgr_base + CHGR_IBAT_TERM_CHGR,
				QPNP_CHG_ITERM_MASK, temp, 1);
	}

	temp = (term_current - QPNP_CHG_ITERM_MIN_MA)
				/ QPNP_CHG_ITERM_STEP_MA;

	pr_info("ibatterm=%dmA setting %02x\n", term_current, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_TERM_CHGR,
			QPNP_CHG_ITERM_MASK, temp, 1);
}

#define QPNP_CHG_IBATMAX_MIN	50
#define QPNP_CHG_IBATMAX_MAX	3250
static int
qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current)
{
	u8 temp;

	if (chg_current < QPNP_CHG_IBATMAX_MIN
			|| chg_current > QPNP_CHG_IBATMAX_MAX) {
		pr_err("bad mA=%d asked to set\n", chg_current);
		return -EINVAL;
	}
	temp = chg_current / QPNP_CHG_I_STEP_MA;

	pr_info("ibatmax=%dmA setting %02x\n", chg_current, temp);
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_IBAT_MAX,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_TCHG_MASK	0x7F
#define QPNP_CHG_TCHG_EN_MASK  0x80
#define QPNP_CHG_TCHG_MIN	4
#define QPNP_CHG_TCHG_MAX	512
#define QPNP_CHG_TCHG_STEP	4
static int qpnp_chg_tchg_max_set(struct qpnp_chg_chip *chip, int minutes)
{
	u8 temp;
	int rc;

	if (minutes < QPNP_CHG_TCHG_MIN || minutes > QPNP_CHG_TCHG_MAX) {
		pr_err("bad max minutes =%d asked to set\n", minutes);
		return -EINVAL;
	}

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, 0, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	temp = minutes / QPNP_CHG_TCHG_STEP - 1;

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX,
			QPNP_CHG_TCHG_MASK, temp, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}
	pr_info("minutes=%d setting %02x\n", minutes, temp);

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, QPNP_CHG_TCHG_EN_MASK, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	return 0;
}

#define QPNP_CHG_V_MIN_MV	3240
#define QPNP_CHG_V_MAX_MV	4500
#define QPNP_CHG_V_STEP_MV	10
static int
qpnp_chg_vddsafe_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_V_MIN_MV
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	temp = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	pr_info("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VDD_SAFE, 1);
}

static int
qpnp_chg_vddmax_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp = 0;

	if (voltage < QPNP_CHG_VDDMAX_MIN
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	chip->set_vddmax_mv = voltage + chip->delta_vddmax_mv;

	temp = (chip->set_vddmax_mv - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;

	pr_info("voltage=%d setting %02x\n", chip->set_vddmax_mv, temp);
	return qpnp_chg_write(chip, &temp, chip->chgr_base + CHGR_VDD_MAX, 1);
}

#define BOOST_MIN_UV	4200000
#define BOOST_MAX_UV	5500000
#define BOOST_STEP_UV	50000
#define BOOST_MIN	16
#define N_BOOST_V	((BOOST_MAX_UV - BOOST_MIN_UV) / BOOST_STEP_UV + 1)
static int
qpnp_boost_vset(struct qpnp_chg_chip *chip, int voltage)
{
	u8 reg = 0;

	if (voltage < BOOST_MIN_UV || voltage > BOOST_MAX_UV) {
		pr_err("invalid voltage requested %d uV\n", voltage);
		return -EINVAL;
	}

	reg = DIV_ROUND_UP(voltage - BOOST_MIN_UV, BOOST_STEP_UV) + BOOST_MIN;

	pr_debug("voltage=%d setting %02x\n", voltage, reg);
	return qpnp_chg_write(chip, &reg, chip->boost_base + BOOST_VSET, 1);
}

static int
qpnp_boost_vget_uv(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 boost_reg;

	rc = qpnp_chg_read(chip, &boost_reg,
		 chip->boost_base + BOOST_VSET, 1);
	if (rc) {
		pr_err("failed to read BOOST_VSET rc=%d\n", rc);
		return rc;
	}

	if (boost_reg < BOOST_MIN) {
		pr_err("Invalid reading from 0x%x\n", boost_reg);
		return -EINVAL;
	}

	return BOOST_MIN_UV + ((boost_reg - BOOST_MIN) * BOOST_STEP_UV);
}

/* JEITA compliance logic */
static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vddmax_set(chip, chip->cool_bat_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vddmax_set(chip, chip->warm_bat_mv);
	else
		qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
}

static DEFINE_SPINLOCK(set_current_lock);
static void
qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;
	unsigned long flags;
	struct qpnp_vadc_result result;
	int usbin = 0, rc = 0;

	rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &result);
	if (rc)
		pr_warn("error reading USBIN channel = %d, rc = %d\n",
		USBIN, rc);
	usbin = (int)result.physical;
	spin_lock_irqsave(&set_current_lock, flags);
	/* change limit current if temperature is changed */
	if (htc_battery_is_support_qc20() &&
			is_ac_online() &&
			htc_battery_check_cable_type_from_usb() == DWC3_DCP &&
			usbin > QC20_9_VOLT_CHECK)
		chg_current = chip->qc20_ibatmax;

	if (chip->bat_is_cool)
		chg_current = min(chg_current, chip->cool_bat_chg_ma);

	if (chip->bat_is_warm)
		chg_current = min(chg_current, chip->warm_bat_chg_ma);

	if (thermal_mitigation != 0 && chip->thermal_mitigation)
		chg_current = min(chg_current,
			chip->thermal_mitigation[thermal_mitigation]);

	if (chg_limit_current != 0)
		chg_current = min(chg_current, chg_limit_current);

	pr_info("setting: %dmA, usbin: %duv\n", chg_current, usbin);
	qpnp_chg_ibatmax_set(chip, chg_current);
	spin_unlock_irqrestore(&set_current_lock, flags);
}

#if !(defined(CONFIG_HTC_BATT_8960))
static void
qpnp_batt_system_temp_level_set(struct qpnp_chg_chip *chip, int lvl_sel)
{
	if (lvl_sel >= 0 && lvl_sel < chip->thermal_levels) {
		chip->therm_lvl_sel = lvl_sel;
		if (lvl_sel == (chip->thermal_levels - 1)) {
			/* disable charging if highest value selected */
			qpnp_chg_buck_control(chip, 0);
		} else {
			qpnp_chg_buck_control(chip, 1);
			qpnp_chg_set_appropriate_battery_current(chip);
		}
	} else {
		pr_err("Unsupported level selected %d\n", lvl_sel);
	}
}
#endif /* !CONFIG_HTC_BATT_8960 */

/* OTG regulator operations */
static int
qpnp_chg_regulator_otg_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_host_mode(chip);
}

static int
qpnp_chg_regulator_otg_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_charge_mode(chip);
}

static int
qpnp_chg_regulator_otg_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_otg_en_set(chip);
}

static int
qpnp_chg_regulator_boost_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			(chip->flags & BOOST_FLASH_WA)) {
		qpnp_chg_usb_suspend_enable(chip, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x2F, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	return qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		BOOST_PWR_EN, 1);
}

/* Boost regulator operations */
#define ABOVE_VBAT_WEAK		BIT(1)
static int
qpnp_chg_regulator_boost_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 vbat_sts;

	rc = qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		0, 1);
	if (rc) {
		pr_err("failed to disable boost rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_read(chip, &vbat_sts,
			chip->chgr_base + CHGR_VBAT_STATUS, 1);
	if (rc) {
		pr_err("failed to read bat sts rc=%d\n", rc);
		return rc;
	}

	if (!(vbat_sts & ABOVE_VBAT_WEAK) && (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x20, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(2000);

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	if (qpnp_chg_is_usb_chg_plugged_in(chip)
			&& (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(1000);

		qpnp_chg_usb_suspend_enable(chip, 0);
	}

	return rc;
}

static int
qpnp_chg_regulator_boost_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_boost_en_set(chip);
}

static int
qpnp_chg_regulator_boost_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	int uV = min_uV;
	int rc;
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	if (uV < BOOST_MIN_UV && max_uV >= BOOST_MIN_UV)
		uV = BOOST_MIN_UV;


	if (uV < BOOST_MIN_UV || uV > BOOST_MAX_UV) {
		pr_err("request %d uV is out of bounds\n", uV);
		return -EINVAL;
	}

	*selector = DIV_ROUND_UP(uV - BOOST_MIN_UV, BOOST_STEP_UV);
	if ((*selector * BOOST_STEP_UV + BOOST_MIN_UV) > max_uV) {
		pr_err("no available setpoint [%d, %d] uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	rc = qpnp_boost_vset(chip, uV);

	return rc;
}

static int
qpnp_chg_regulator_boost_get_voltage(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_boost_vget_uv(chip);
}

static int
qpnp_chg_regulator_boost_list_voltage(struct regulator_dev *rdev,
			unsigned selector)
{
	if (selector >= N_BOOST_V)
		return 0;

	return BOOST_MIN_UV + (selector * BOOST_STEP_UV);
}

static struct regulator_ops qpnp_chg_otg_reg_ops = {
	.enable			= qpnp_chg_regulator_otg_enable,
	.disable		= qpnp_chg_regulator_otg_disable,
	.is_enabled		= qpnp_chg_regulator_otg_is_enabled,
};

static struct regulator_ops qpnp_chg_boost_reg_ops = {
	.enable			= qpnp_chg_regulator_boost_enable,
	.disable		= qpnp_chg_regulator_boost_disable,
	.is_enabled		= qpnp_chg_regulator_boost_is_enabled,
	.set_voltage		= qpnp_chg_regulator_boost_set_voltage,
	.get_voltage		= qpnp_chg_regulator_boost_get_voltage,
	.list_voltage		= qpnp_chg_regulator_boost_list_voltage,
};

#define BATFET_LPM_MASK		0xC0
#define BATFET_LPM		0x40
#define BATFET_NO_LPM		0x00
static int
qpnp_chg_regulator_batfet_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_SPARE,
			BATFET_LPM_MASK, BATFET_NO_LPM, 1);
	if (rc)
		pr_err("failed to write to batt_if rc=%d\n", rc);
	return rc;
}

static int
qpnp_chg_regulator_batfet_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_SPARE,
			BATFET_LPM_MASK, BATFET_LPM, 1);
	if (rc)
		pr_err("failed to write to batt_if rc=%d\n", rc);
	return rc;
}

static int
qpnp_chg_regulator_batfet_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg,
				chip->bat_if_base + CHGR_BAT_IF_SPARE, 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return rc;
	}

	if (reg && BATFET_LPM_MASK == BATFET_NO_LPM)
		return 1;

	return 0;
}

static struct regulator_ops qpnp_chg_batfet_vreg_ops = {
	.enable			= qpnp_chg_regulator_batfet_enable,
	.disable		= qpnp_chg_regulator_batfet_disable,
	.is_enabled		= qpnp_chg_regulator_batfet_is_enabled,
};

#if !(defined(CONFIG_HTC_BATT_8960))
#define MIN_DELTA_MV_TO_INCREASE_VDD_MAX	13
/*FIXME to load MAX_DELTA_VDD_MAX_MV value throuh board or device tree*/
#define MAX_DELTA_VDD_MAX_MV			10
static void
qpnp_chg_adjust_vddmax(struct qpnp_chg_chip *chip, int vbat_mv)
{
	int delta_mv, closest_delta_mv, sign;

	delta_mv = chip->max_voltage_mv - vbat_mv;
	if (delta_mv > 0 && delta_mv < MIN_DELTA_MV_TO_INCREASE_VDD_MAX) {
		pr_debug("vbat is not low enough to increase vdd\n");
		return;
	}

	sign = delta_mv > 0 ? 1 : -1;
	closest_delta_mv = ((delta_mv + sign * QPNP_CHG_V_STEP_MV / 2)
			/ QPNP_CHG_V_STEP_MV) * QPNP_CHG_V_STEP_MV;
	pr_debug("max_voltage = %d, vbat_mv = %d, delta_mv = %d, closest = %d\n",
			chip->max_voltage_mv, vbat_mv,
			delta_mv, closest_delta_mv);
	chip->delta_vddmax_mv = clamp(chip->delta_vddmax_mv + closest_delta_mv,
			-MAX_DELTA_VDD_MAX_MV, MAX_DELTA_VDD_MAX_MV);
	pr_debug("using delta_vddmax_mv = %d\n", chip->delta_vddmax_mv);
	qpnp_chg_set_appropriate_vddmax(chip);
}
/* HTC used */
#else

#define CHG_VDDMAX_ADJUST_DELTA_10_MV	10
#define CHG_VDDMAX_ADJUST_DELTA_20_MV	20
#define CHG_VDDMAX_ADJUST_DELTA_30_MV	30
#define CHG_VDDMAX_DIFF_15_MV	15
#define CHG_VDDMAX_DIFF_30_MV	30
#define CHG_VDDMAX_DIFF_40_MV	40

static void
qpnp_chg_adjust_vddmax(struct qpnp_chg_chip *chip, int vbat_mv)
{
	int delta_vddmax_mv = 0;

	if (chip->bat_is_cool || chip->bat_is_warm) {
		pr_info("Not adjust vddmax, warm=%d, cool=%d\n",
				chip->bat_is_warm, chip->bat_is_cool);
		return;
	}

	if (vbat_mv <= chip->max_voltage_mv - CHG_VDDMAX_DIFF_40_MV)
		delta_vddmax_mv = CHG_VDDMAX_ADJUST_DELTA_30_MV;
	else if (vbat_mv <= chip->max_voltage_mv - CHG_VDDMAX_DIFF_30_MV)
		delta_vddmax_mv = CHG_VDDMAX_ADJUST_DELTA_20_MV;
	else if (vbat_mv <= chip->max_voltage_mv - CHG_VDDMAX_DIFF_15_MV)
		delta_vddmax_mv = CHG_VDDMAX_ADJUST_DELTA_10_MV;

    pr_info("delta_vddmax_mv=%d, vbat=%d, max_voltage_mv=%d, pre_delta_vddmax_mv=%d\n",
			delta_vddmax_mv, vbat_mv, chip->max_voltage_mv, chip->pre_delta_vddmax_mv);

	if (delta_vddmax_mv < chip->pre_delta_vddmax_mv)
		delta_vddmax_mv = chip->pre_delta_vddmax_mv;

	if (delta_vddmax_mv) {
		chip->pre_delta_vddmax_mv =
			chip->delta_vddmax_mv = delta_vddmax_mv;
		qpnp_chg_set_appropriate_vddmax(chip);
	}
}
#endif

#define CONSECUTIVE_COUNT	3
#define VBATDET_MAX_ERR_MV	50
#define CLEAR_FULL_STATE_BY_LEVEL_THR		90
static void
qpnp_eoc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, eoc_work);
	int ibat_ma, vbat_mv, rc = 0, soc = 0;
	u8 batt_sts = 0, buck_sts = 0, chg_sts = 0;
#if !(defined(CONFIG_HTC_BATT_8960))
	static int vbat_low_count;
	bool vbat_lower_than_vbatdet;
#endif

	pm_stay_awake(chip->dev);
#if !(defined(CONFIG_HTC_BATT_8960))
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
#endif

	if (!is_ac_safety_timeout) {
		rc = qpnp_chg_masked_write(chip,
				chip->chgr_base + CHGR_CHG_FAILED,
				CHGR_CHG_FAILED_BIT,
				CHGR_CHG_FAILED_BIT, 1);
		if (rc)
			pr_err("Failed to write chg_fail clear bit!\n");
	}

	rc = qpnp_chg_read(chip, &batt_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		goto stop_eoc;
	}

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck rc=%d\n", rc);
		goto stop_eoc;
	}

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read chg_sts rc=%d\n", rc);
		goto stop_eoc;
	}

	pr_info("chgr: 0x%x, bat_if: 0x%x, buck: 0x%x\n",
		chg_sts, batt_sts, buck_sts);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_info("no chg connected, stopping\n");
		if (is_batt_full)
				pm8941_bms_batt_full_fake_ocv();
		is_batt_full = false;
		is_batt_full_eoc_stop = false;
		goto stop_eoc;
	}

	if ((batt_sts & BAT_FET_ON_IRQ) && (chg_sts & FAST_CHG_ON_IRQ
					|| chg_sts & TRKL_CHG_ON_IRQ)) {
#if !(defined(CONFIG_HTC_BATT_8960))
		ibat_ma = get_prop_current_now(chip) / 1000;
#else
		pm8941_bms_get_batt_current(&ibat_ma);
		ibat_ma = ibat_ma / 1000;
#endif
		vbat_mv = get_prop_battery_voltage_now(chip) / 1000;

		pr_info("ibat_ma = %d vbat_mv = %d term_current_ma = %d\n",
				ibat_ma, vbat_mv, chip->term_current);

		/* To always hold eoc_wake_lock if cable exist,
			due to vbatdet_lo can't occur during suspend*/
#if !(defined(CONFIG_HTC_BATT_8960))
		vbat_lower_than_vbatdet = !(chg_sts & VBAT_DET_LOW_IRQ);
		if (vbat_lower_than_vbatdet && vbat_mv <
				(chip->max_voltage_mv - chip->resume_delta_mv
				 - VBATDET_MAX_ERR_MV)) {
			vbat_low_count++;
			pr_debug("woke up too early vbat_mv = %d, max_mv = %d, resume_mv = %d tolerance_mv = %d low_count = %d\n",
					vbat_mv, chip->max_voltage_mv,
					chip->resume_delta_mv,
					VBATDET_MAX_ERR_MV, vbat_low_count);
			if (vbat_low_count >= CONSECUTIVE_COUNT) {
				pr_debug("woke up too early stopping\n");
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				goto check_again_later;
			}
		} else {
			vbat_low_count = 0;
		}
#endif

#if !(defined(CONFIG_HTC_BATT_8960))
		if ((buck_sts & VDD_LOOP_IRQ) && (chip->enable_qct_adjust_vddmax))
			qpnp_chg_adjust_vddmax(chip, vbat_mv);
#endif

		/* Retry AICL in 5 minutes if usb_ma is only 500 mA */
		if (chip->is_pm8921_aicl_enabled
				&& chip->retry_aicl_cnt)
			retry_aicl_mechanism(chip);

		/* Regulate VIN_MIN */
		if (chip->regulate_vin_min_thr_mv
				&& chip->lower_vin_min
				&& !htc_battery_is_support_qc20())
			adjust_chg_vin_min(chip, 1);

		if (!(buck_sts & VDD_LOOP_IRQ)) {
			pr_debug("Not in CV\n");
			eoc_count_by_curr = eoc_count = 0;
		} else if (ibat_ma > 0) {
			/* battery is discharging */
			pr_debug("Charging but system demand increased\n");
			eoc_count_by_curr = eoc_count = 0;
		} else if ((ibat_ma * -1) > chip->term_current) {
			/* charging current >= term_current criterion */
			pr_debug("Not at EOC, battery current too high\n");
			eoc_count_by_curr = eoc_count = 0;
		} else if ((ibat_ma * -1) > chip->eoc_ibat_thre_ma) {
			/* eoc_ibat_thre_ma criterion < charging current */
			pr_debug("start count for HTC battery full condition\n");
			eoc_count_by_curr = 0;
			eoc_count++;
			pr_info("eoc_count = %d\n", eoc_count);
			if (eoc_count == CONSECUTIVE_COUNT) {
				is_batt_full = true;
/* HTC used */
#if (defined(CONFIG_HTC_BATT_8960))
				if (chip->enable_qct_adjust_vddmax)
					qpnp_chg_adjust_vddmax(chip, vbat_mv);
				/* Configure vin_min setting to default value for QC 2.0 issue */
				if (htc_battery_is_support_qc20()) {
					if (chip->regulate_vin_min_thr_mv
							&& chip->lower_vin_min)
						adjust_chg_vin_min(chip, 1);
					else
						qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
				}
#endif
				htc_gauge_event_notify(HTC_GAUGE_EVENT_EOC);
			}
		} else {
			eoc_count++;
			/* charging current < eoc_ibat_thre_ma criterion */
			if (eoc_count_by_curr == CONSECUTIVE_COUNT) {
				pr_info("End of Charging\n");
				chip->delta_vddmax_mv = 0;
				/* Keep VDD_MAX after charging full. HW: re-set
				VDD_MAX make VPH_PWR very closes to VBAT.
				This causes enter re-charging cycle soon. */
				/* qpnp_chg_set_appropriate_vddmax(chip); */
				chip->chg_done = true;
				is_batt_full_eoc_stop = true;
				qpnp_chg_set_appropriate_vbatdet(chip);

#if !(defined(CONFIG_HTC_BATT_8960))
				pr_debug("psy changed batt_psy\n");
				power_supply_changed(&chip->batt_psy);
#endif
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				if (eoc_count == CONSECUTIVE_COUNT && !is_batt_full) {
					is_batt_full = true;
					htc_gauge_event_notify(HTC_GAUGE_EVENT_EOC);
				}
				eoc_count_by_curr += 1;
				pr_info("eoc_count_by_curr = %d, eoc_count=%d\n",
					eoc_count_by_curr, eoc_count);
			}
		}

		/* Check if overloading is happened after full charging.
		If CLEAR_FULL_STATE_BY_LEVEL_THR is set too high(99),
		you might meet soc can't report higher soc due to
		start_percent is -22 even if overloading status has been removed. */
		if (is_batt_full) {
			pm8941_bms_get_batt_soc(&soc);
			if (soc < CLEAR_FULL_STATE_BY_LEVEL_THR) {
				vbat_mv = get_prop_battery_voltage_now(chip) / 1000;;
				/* We add one more condition to check overloading */
				if (chip->max_voltage_mv &&
					(vbat_mv > (chip->max_voltage_mv - 100))) {
					pr_info("Not satisfy overloading battery voltage"
						" critiria (%dmV < %dmV).\n", vbat_mv,
						(chip->max_voltage_mv - 100));
				} else {
					is_batt_full = false;
					eoc_count = eoc_count_by_curr = 0;
					pr_info("%s: Clear is_batt_full & eoc_count due to"
						" Overloading happened, soc=%d\n",
						__func__, soc);
					htc_gauge_event_notify(HTC_GAUGE_EVENT_EOC);
				}
			}
		}
	} else {
		pr_info("not charging\n");
			goto stop_eoc;
	}
#if !(defined(CONFIG_HTC_BATT_8960))
check_again_later:
#endif
	schedule_delayed_work(&chip->eoc_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	return;

stop_eoc:
#if !(defined(CONFIG_HTC_BATT_8960))
	vbat_low_count = 0;
#endif
	eoc_count_by_curr = eoc_count = 0;
	is_ac_safety_timeout_twice = false;
	pm_relax(chip->dev);
}

#if !(defined(CONFIG_HTC_BATT_8960))
static void
qpnp_chg_soc_check_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, soc_check_work);

	get_prop_capacity(chip);
}
#endif

static void
qpnp_chg_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	struct qpnp_chg_chip *chip = ctx;
	bool bat_warm = 0, bat_cool = 0;
	int temp;

	if (state >= ADC_TM_STATE_NUM) {
		pr_err("invalid notification %d\n", state);
		return;
	}

	temp = get_prop_batt_temp(chip);

	pr_info("temp = %d state = %s\n", temp,
			state == ADC_TM_WARM_STATE ? "warm" : "cool");

	if (state == ADC_TM_WARM_STATE) {
		if (temp > chip->warm_bat_decidegc) {
			/* Normal to warm */
			bat_warm = true;
			bat_cool = false;
			chip->adc_param.low_temp =
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_COOL_THR_ENABLE;
		} else if (temp >
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC){
			/* Cool to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	} else {
		if (temp < chip->cool_bat_decidegc) {
			/* Normal to cool */
			bat_warm = false;
			bat_cool = true;
			chip->adc_param.high_temp =
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_WARM_THR_ENABLE;
		} else if (temp <
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC){
			/* Warm to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	}

	if (chip->bat_is_cool ^ bat_cool || chip->bat_is_warm ^ bat_warm) {
		chip->bat_is_cool = bat_cool;
		chip->bat_is_warm = bat_warm;

#if !(defined(CONFIG_HTC_BATT_8960))
		if (bat_cool || bat_warm)
			chip->resuming_charging = false;
#endif

		/**
		 * set appropriate voltages and currents.
		 *
		 * Note that when the battery is hot or cold, the charger
		 * driver will not resume with SoC. Only vbatdet is used to
		 * determine resume of charging.
		 */
		qpnp_chg_set_appropriate_vddmax(chip);
		qpnp_chg_set_appropriate_battery_current(chip);
		qpnp_chg_set_appropriate_vbatdet(chip);
		htc_gauge_event_notify(HTC_GAUGE_EVENT_TEMP_ZONE_CHANGE);
		if (chip->ext_ovpfet_gpio && is_ac_online() &&
			!chip->bat_is_cool && !chip->bat_is_warm) {
			if (!delayed_work_pending(&chip->ovpfet_resistance_check_work)) {
				pr_info("re-enable ovpfet_resistance_check_work.\n");
				schedule_delayed_work(&chip->ovpfet_resistance_check_work,
					msecs_to_jiffies(OVPFET_R_CHECK_WAIT_MS));
			}
		}
	}

	pr_debug("warm %d, cool %d, low = %d deciDegC, high = %d deciDegC\n",
			chip->bat_is_warm, chip->bat_is_cool,
			chip->adc_param.low_temp, chip->adc_param.high_temp);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");

	pr_info("batt_temp:%d, bat_is_cool:%d, bat_is_warm:%d, bat_cool:%d, bat_warm:%d\n",
		temp, chip->bat_is_cool, chip->bat_is_warm, bat_cool, bat_warm);
}

#if !(defined(CONFIG_HTC_BATT_8960))
#define MIN_COOL_TEMP	-300
#define MAX_WARM_TEMP	1000

static int
qpnp_chg_configure_jeita(struct qpnp_chg_chip *chip,
		enum power_supply_property psp, int temp_degc)
{
	int rc = 0;

	if ((temp_degc < MIN_COOL_TEMP) || (temp_degc > MAX_WARM_TEMP)) {
		pr_err("Bad temperature request %d\n", temp_degc);
		return -EINVAL;
	}

	mutex_lock(&chip->jeita_configure_lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		if (temp_degc >=
			(chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set cool %d higher than warm %d - hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_cool)
			chip->adc_param.high_temp =
				temp_degc + HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_warm)
			chip->adc_param.low_temp = temp_degc;

		chip->cool_bat_decidegc = temp_degc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		if (temp_degc <=
			(chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set warm %d higher than cool %d + hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_warm)
			chip->adc_param.low_temp =
				temp_degc - HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_cool)
			chip->adc_param.high_temp = temp_degc;

		chip->warm_bat_decidegc = temp_degc;
		break;
	default:
		rc = -EINVAL;
		goto mutex_unlock;
	}

	schedule_work(&chip->adc_measure_work);

mutex_unlock:
	mutex_unlock(&chip->jeita_configure_lock);
	return rc;
}

static int
qpnp_dc_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (!val->intval)
			break;

		rc = qpnp_chg_idcmax_set(chip, val->intval / 1000);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			return rc;
		}
		chip->maxinput_dc_ma = (val->intval / 1000);

		break;
	default:
		return -EINVAL;
	}
#if !(defined(CONFIG_HTC_BATT_8960))
	pr_debug("psy changed dc_psy\n");
	power_supply_changed(&chip->dc_psy);
#endif
	return rc;
}

static int
qpnp_batt_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->charging_disabled = !(val->intval);
		if (chip->charging_disabled) {
			/* disable charging */
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
			qpnp_chg_force_run_on_batt(chip,
						chip->charging_disabled);
		} else {
			/* enable charging */
			qpnp_chg_force_run_on_batt(chip,
					chip->charging_disabled);
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		qpnp_batt_system_temp_level_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		qpnp_chg_iusbmax_set(chip, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		qpnp_chg_vinmin_set(chip, val->intval / 1000);
		break;
	default:
		return -EINVAL;
	}

#if !(defined(CONFIG_HTC_BATT_8960))
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
	return rc;
}
#endif /* !CONFIG_HTC_BATT_8960 */

#define POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS		20
#define POWER_STAGE_REDUCE_MAX_VBAT_UV			3900000
#define POWER_STAGE_REDUCE_MIN_VCHG_UV			4800000
#define POWER_STAGE_SEL_MASK				0x0F
#define POWER_STAGE_REDUCED				0x01
#define POWER_STAGE_DEFAULT				0x0F
static int
qpnp_chg_power_stage_set(struct qpnp_chg_chip *chip, bool reduce)
{
	int rc;
	u8 reg = 0xA5;

	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + SEC_ACCESS,
				 1);
	if (rc) {
		pr_err("Error %d writing 0xA5 to buck's 0x%x reg\n",
				rc, SEC_ACCESS);
		return rc;
	}

	reg = POWER_STAGE_DEFAULT;
	if (reduce)
		reg = POWER_STAGE_REDUCED;
	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);

	if (rc)
		pr_err("Error %d writing 0x%x power stage register\n", rc, reg);
	return rc;
}

static int
qpnp_chg_get_vusbin_uv(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &results);
	if (rc) {
		pr_err("Unable to read vbat rc=%d\n", rc);
		return 0;
	}
	return results.physical;
}


static
int get_vusb_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vusb_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vusb_uv += qpnp_chg_get_vusbin_uv(chip);

	vusb_uv = vusb_uv / sample_count;
	return vusb_uv;
}

static
int get_vbat_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vbat_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vbat_uv += get_prop_battery_voltage_now(chip);

	vbat_uv = vbat_uv / sample_count;
	return vbat_uv;
}


static bool
qpnp_chg_is_power_stage_reduced(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);
	if (rc) {
		pr_err("Error %d reading power stage register\n", rc);
		return false;
	}

	if ((reg & POWER_STAGE_SEL_MASK) == POWER_STAGE_DEFAULT)
		return false;

	return true;
}


static void
qpnp_chg_reduce_power_stage(struct qpnp_chg_chip *chip)
{
	struct timespec ts;
	bool power_stage_reduced_in_hw = qpnp_chg_is_power_stage_reduced(chip);
	bool reduce_power_stage = false;
	int vbat_uv = get_vbat_averaged(chip, 16);
	int vusb_uv = get_vusb_averaged(chip, 16);
	bool fast_chg =
		(get_prop_charge_type(chip) == POWER_SUPPLY_CHARGE_TYPE_FAST);
	static int count_restore_power_stage;
	static int count_reduce_power_stage;
	bool vchg_loop = get_prop_vchg_loop(chip);
	bool ichg_loop = qpnp_chg_is_ichg_loop_active(chip);
	bool usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	bool usb_ma_above_wall =
		(qpnp_chg_usb_iusbmax_get(chip) > USB_WALL_THRESHOLD_MA);

	if (fast_chg
		&& usb_present
		&& usb_ma_above_wall
		&& vbat_uv < POWER_STAGE_REDUCE_MAX_VBAT_UV
		&& vusb_uv > POWER_STAGE_REDUCE_MIN_VCHG_UV)
		reduce_power_stage = true;

	if ((usb_present && usb_ma_above_wall)
		&& (vchg_loop || ichg_loop))
		reduce_power_stage = true;

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		count_restore_power_stage++;
		count_reduce_power_stage = 0;
	} else if (!power_stage_reduced_in_hw && reduce_power_stage) {
		count_reduce_power_stage++;
		count_restore_power_stage = 0;
	} else if (power_stage_reduced_in_hw == reduce_power_stage) {
		count_restore_power_stage = 0;
		count_reduce_power_stage = 0;
	}

	pr_debug("power_stage_hw = %d reduce_power_stage = %d usb_present = %d usb_ma_above_wall = %d vbat_uv(16) = %d vusb_uv(16) = %d fast_chg = %d , ichg = %d, vchg = %d, restore,reduce = %d, %d\n",
			power_stage_reduced_in_hw, reduce_power_stage,
			usb_present, usb_ma_above_wall,
			vbat_uv, vusb_uv, fast_chg,
			ichg_loop, vchg_loop,
			count_restore_power_stage, count_reduce_power_stage);

	if (!power_stage_reduced_in_hw && reduce_power_stage) {
		if (count_reduce_power_stage >= 2) {
			qpnp_chg_power_stage_set(chip, true);
			power_stage_reduced_in_hw = true;
		}
	}

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		if (count_restore_power_stage >= 6
				|| (!usb_present || !usb_ma_above_wall)) {
			qpnp_chg_power_stage_set(chip, false);
			power_stage_reduced_in_hw = false;
		}
	}

	if (usb_present && usb_ma_above_wall) {
		getnstimeofday(&ts);
		ts.tv_sec += POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS;
		alarm_start_range(&chip->reduce_power_stage_alarm,
					timespec_to_ktime(ts),
					timespec_to_ktime(ts));
	} else {
		pr_debug("stopping power stage workaround\n");
		chip->power_stage_workaround_running = false;
	}
}


static void
qpnp_chg_reduce_power_stage_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, reduce_power_stage_work);

	qpnp_chg_reduce_power_stage(chip);
}

static void
qpnp_chg_reduce_power_stage_callback(struct alarm *alarm)
{
	struct qpnp_chg_chip *chip = container_of(alarm, struct qpnp_chg_chip,
						reduce_power_stage_alarm);

	schedule_work(&chip->reduce_power_stage_work);
}

static int
qpnp_chg_setup_flags(struct qpnp_chg_chip *chip)
{
	if (chip->revision > 0 && chip->type == SMBB)
		chip->flags |= CHG_FLAGS_VCP_WA;
	if (chip->type == SMBB)
		chip->flags |= BOOST_FLASH_WA;
	if (chip->type == SMBBP) {
		struct device_node *revid_dev_node;
		struct pmic_revid_data *revid_data;

		chip->flags |=  BOOST_FLASH_WA;

		revid_dev_node = of_parse_phandle(chip->spmi->dev.of_node,
						"qcom,pmic-revid", 0);
		if (!revid_dev_node) {
			pr_err("Missing qcom,pmic-revid property\n");
			return -EINVAL;
		}
		revid_data = get_revid_data(revid_dev_node);
		if (IS_ERR(revid_data)) {
			pr_err("Couldnt get revid data rc = %ld\n",
						PTR_ERR(revid_data));
			return PTR_ERR(revid_data);
		}

		if (revid_data->rev4 < PM8226_V2P1_REV4
			|| ((revid_data->rev4 == PM8226_V2P1_REV4)
				&& (revid_data->rev3 <= PM8226_V2P1_REV3))) {
			chip->flags |= POWER_STAGE_WA;
		}
	}
	return 0;
}

static int
qpnp_chg_request_irqs(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	u8 subtype;
	struct spmi_device *spmi = chip->spmi;

	spmi_for_each_container_dev(spmi_resource, chip->spmi) {
		if (!spmi_resource) {
				pr_err("qpnp_chg: spmi resource absent\n");
			return rc;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			return rc;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			return rc;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chg_fastchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "fast-chg-on");
			if (chip->chg_fastchg.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			chip->chg_trklchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "trkl-chg-on");
			if (chip->chg_trklchg.irq < 0) {
				pr_err("Unable to get trkl-chg-on irq\n");
				return rc;
			}

			chip->chg_failed.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-failed");
			if (chip->chg_failed.irq < 0) {
				pr_err("Unable to get chg_failed irq\n");
				return rc;
			}

			chip->chg_vbatdet_lo.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-det-lo");
			if (chip->chg_vbatdet_lo.irq < 0) {
				pr_err("Unable to get vbat-det-lo irq\n");
				return rc;
			}

			chip->chg_vbatdet_hi.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-det-hi");
			if (chip->chg_vbatdet_hi.irq < 0) {
				pr_err("Unable to get vbat-det-hi irq\n");
				return rc;
			}

			chip->chgwdog.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chgwdog");
			if (chip->chgwdog.irq < 0) {
				pr_err("Unable to get chgwdog irq\n");
				return rc;
			}

			chip->chg_state_change.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "state-change");
			if (chip->chg_state_change.irq < 0) {
				pr_err("Unable to get state-change irq\n");
				return rc;
			}

			chip->chg_is_done.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-done");
			if (chip->chg_is_done.irq < 0) {
				pr_err("Unable to get chg-done irq\n");
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_failed.irq,
				qpnp_chg_chgr_chg_failed_irq_handler,
				IRQF_TRIGGER_RISING, "chg-failed", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-failed: %d\n",
						chip->chg_failed.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_fastchg.irq,
					qpnp_chg_chgr_chg_fastchg_irq_handler,
					IRQF_TRIGGER_RISING,
					"fast-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d fast-chg-on: %d\n",
						chip->chg_fastchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_trklchg.irq,
				qpnp_chg_chgr_chg_trklchg_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"trkl-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d trkl-chg-on: %d\n",
						chip->chg_trklchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev,
				chip->chg_vbatdet_lo.irq,
				qpnp_chg_vbatdet_lo_irq_handler,
				IRQF_TRIGGER_RISING,
				"vbat-det-lo", chip);
			if (rc < 0) {
				pr_err("Can't request %d vbat-det-lo: %d\n",
						chip->chg_vbatdet_lo.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->chg_trklchg.irq);
			enable_irq_wake(chip->chg_failed.irq);
			qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);
			enable_irq_wake(chip->chg_vbatdet_lo.irq);

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->batt_pres.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "batt-pres");
			if (chip->batt_pres.irq < 0) {
				pr_err("Unable to get batt-pres irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_pres.irq,
				qpnp_chg_bat_if_batt_pres_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
				| IRQF_SHARED | IRQF_ONESHOT,
				"batt-pres", chip);
			if (rc < 0) {
				pr_err("Can't request %d batt-pres irq: %d\n",
						chip->batt_pres.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->batt_pres.irq);

			chip->batt_temp_ok.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "bat-temp-ok");
			if (chip->batt_temp_ok.irq < 0) {
				pr_err("Unable to get bat-temp-ok irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_temp_ok.irq,
				qpnp_chg_bat_if_batt_temp_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"bat-temp-ok", chip);
			if (rc < 0) {
				pr_err("Can't request %d bat-temp-ok irq: %d\n",
						chip->batt_temp_ok.irq, rc);
				return rc;
			}

#ifdef CONFIG_ARCH_MSM8226
			qpnp_chg_bat_if_batt_temp_irq_handler(0, chip);
#endif

			enable_irq_wake(chip->batt_temp_ok.irq);

			chip->batt_fet_on.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "bat-fet-on");
			if (chip->batt_fet_on.irq < 0) {
				pr_err("Unable to get bat-fet-on irq\n");
				return rc;
			}

			chip->vcp_on.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vcp-on");
			if (chip->vcp_on.irq < 0) {
				pr_err("Unable to get vcp-on irq\n");
				return rc;
			}

			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			chip->vchg_loop.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vchg-loop");
			if (chip->vchg_loop.irq < 0) {
				pr_err("Unable to get vchg-loop irq\n");
				return rc;
			}

			chip->buck_vbat_ov.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-ov");
			if (chip->buck_vbat_ov.irq < 0) {
				pr_err("Unable to get vbat-ov irq\n");
				return rc;
			}

			chip->buck_vreg_ov.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vreg-ov");
			if (chip->buck_vreg_ov.irq < 0) {
				pr_err("Unable to get vreg-ov irq\n");
				return rc;
			}

			chip->buck_overtemp.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "overtemp");
			if (chip->buck_overtemp.irq < 0) {
				pr_err("Unable to get overtemp irq\n");
				return rc;
			}

			chip->buck_ichg_loop.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "ichg-loop");
			if (chip->buck_ichg_loop.irq < 0) {
				pr_err("Unable to get ichg-loop irq\n");
				return rc;
			}

			chip->buck_ibat_loop.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "ibat-loop");
			if (chip->buck_ibat_loop.irq < 0) {
				pr_err("Unable to get ibat-loop irq\n");
				return rc;
			}

			chip->buck_vdd_loop.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vdd-loop");
			if (chip->buck_vdd_loop.irq < 0) {
				pr_err("Unable to get vdd-loop irq\n");
				return rc;
			}
			break;

		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->usbin_valid.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usbin-valid");
			if (chip->usbin_valid.irq < 0) {
				pr_err("Unable to get usbin irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->usbin_valid.irq,
				qpnp_chg_usb_usbin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"usbin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d usbin-valid: %d\n",
						chip->usbin_valid.irq, rc);
				return rc;
			}

			chip->coarse_usb_det.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "coarse-det-usb");
			if (chip->coarse_usb_det.irq < 0) {
				pr_err("Unable to get coarse_usb_det irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->coarse_usb_det.irq,
				qpnp_chg_coarse_det_usb_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"coarse-det-usb", chip);
			if (rc < 0) {
				pr_err("Can't request %d coarse-det-usb: %d\n",
						chip->coarse_usb_det.irq, rc);
				return rc;
			}

			chip->chg_gone.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-gone");
			if (chip->chg_gone.irq < 0) {
				pr_err("Unable to get chg-gone irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->chg_gone.irq,
				qpnp_chg_usb_chg_gone_irq_handler,
				IRQF_TRIGGER_RISING,
					"chg-gone", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-gone: %d\n",
						chip->chg_gone.irq, rc);
				return rc;
			}
			if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
				(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
				chip->usb_ocp.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usb-ocp");
				if (chip->usb_ocp.irq < 0) {
					pr_err("Unable to get usbin irq\n");
					return rc;
				}
				rc = devm_request_irq(chip->dev,
					chip->usb_ocp.irq,
					qpnp_chg_usb_usb_ocp_irq_handler,
					IRQF_TRIGGER_RISING, "usb-ocp", chip);
				if (rc < 0) {
					pr_err("Can't request %d usb-ocp: %d\n",
							chip->usb_ocp.irq, rc);
					return rc;
				}

				enable_irq_wake(chip->usb_ocp.irq);
			}

			enable_irq_wake(chip->usbin_valid.irq);
			enable_irq_wake(chip->coarse_usb_det.irq);
			enable_irq_wake(chip->chg_gone.irq);
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dcin_valid.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "dcin-valid");
			if (chip->dcin_valid.irq < 0) {
				pr_err("Unable to get dcin irq\n");
				return -rc;
			}
			rc = devm_request_irq(chip->dev, chip->dcin_valid.irq,
				qpnp_chg_dc_dcin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"dcin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d dcin-valid: %d\n",
						chip->dcin_valid.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->dcin_valid.irq);

			chip->coarse_dc_det.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "coarse-det-dc");
			if (chip->coarse_dc_det.irq < 0) {
				pr_err("Unable to get coarse-det-dc irq\n");
				return -rc;
			}
			break;
		case SMBB_BOOST_SUBTYPE:
			chip->boost_pwr_ok.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "boost-pwr-ok");
			if (chip->boost_pwr_ok.irq < 0) {
				pr_err("Unable to get boost-pwr-ok irq\n");
				return -rc;
			}

			chip->limit_error.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "limit-error");
			if (chip->limit_error.irq < 0) {
				pr_err("Unable to get limit-error irq\n");
				return -rc;
			}
			break;
		}
	}

	return rc;
}

#define MAX_DELTA_VDD_MAX_MV_VDDSAFE			30
static int
qpnp_chg_load_battery_data(struct qpnp_chg_chip *chip)
{
	struct bms_battery_data batt_data;
	struct device_node *node;
#if !(defined(CONFIG_HTC_BATT_8960))
	struct qpnp_vadc_result result;
#else
	int id_result, battery_id;
#endif
	int rc;

	node = of_find_node_by_name(chip->spmi->dev.of_node,
			"qcom,battery-data");
	if (node) {
		memset(&batt_data, 0, sizeof(struct bms_battery_data));

#if !(defined(CONFIG_HTC_BATT_8960))
		rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX2_BAT_ID, &result);
		if (rc) {
			pr_err("error reading batt id channel = %d, rc = %d\n",
						LR_MUX2_BAT_ID, rc);
			return rc;
		}
#else
		/* assign or detect battery in use currently by ID. */
		battery_id = (int)read_battery_id(chip) / 1000;
		/* translate id_raw to id and set as cur_cell. */
		id_result = htc_battery_cell_find_and_set_id_auto(battery_id);
		pr_info("batt ID vol= %dmv, id_result= %d\n", battery_id, id_result);
#endif

		batt_data.max_voltage_uv = -1;
		batt_data.iterm_ua = -1;

#if !(defined(CONFIG_HTC_BATT_8960))
		rc = of_batterydata_read_data(node,
				&batt_data, result.physical);
#else
		rc = of_batterydata_read_data_by_id_result(node,
				&batt_data, id_result);
#endif
		if (rc) {
			pr_err("failed to read battery data: %d\n", rc);
			return rc;
		}

		if (batt_data.max_voltage_uv >= 0) {
			chip->max_voltage_mv = batt_data.max_voltage_uv / 1000;
			chip->safe_voltage_mv = chip->max_voltage_mv
				+ MAX_DELTA_VDD_MAX_MV_VDDSAFE;
			if (chip->cool_bat_mv > chip->max_voltage_mv)
				chip->cool_bat_mv = chip->max_voltage_mv;
		}
		if (batt_data.iterm_ua >= 0)
			chip->term_current = batt_data.iterm_ua / 1000;

		if (batt_data.qc20_ibatmax_ma >= 0)
			chip->qc20_ibatmax = batt_data.qc20_ibatmax_ma;
		if (batt_data.qc20_ibatsafe_ma >= 0)
			chip->qc20_ibatsafe = batt_data.qc20_ibatsafe_ma;
	}

	return 0;
}

#define WDOG_EN_BIT	BIT(7)
static int
qpnp_chg_hwinit(struct qpnp_chg_chip *chip, u8 subtype,
				struct spmi_resource *spmi_resource)
{
	int rc = 0;
	u8 reg = 0;
	struct regulator_init_data *init_data;
	struct regulator_desc *rdesc;

	switch (subtype) {
	case SMBB_CHGR_SUBTYPE:
	case SMBBP_CHGR_SUBTYPE:
	case SMBCL_CHGR_SUBTYPE:
		if(chip->batt_weak_voltage_mv)
			qpnp_chg_vbatweak_set(chip, chip->batt_weak_voltage_mv);

		if (chip->regulate_vin_min_thr_mv
				&& chip->lower_vin_min)
			rc = adjust_chg_vin_min(chip, 0);
		else
			rc = qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		if (rc) {
			pr_err("failed setting  min_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
		if (rc) {
			pr_err("failed setting max_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vddsafe_set(chip, chip->safe_voltage_mv);
		if (rc) {
			pr_err("failed setting safe_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vbatdet_set(chip,
				QPNP_CHG_VBATDET_MAX_MV);
		if (rc) {
			pr_err("failed setting resume_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		if (rc) {
			pr_err("failed setting ibatmax rc=%d\n", rc);
			return rc;
		}
		if (chip->term_current) {
			rc = qpnp_chg_ibatterm_set(chip, chip->term_current);
			if (rc) {
				pr_err("failed setting ibatterm rc=%d\n", rc);
				return rc;
			}
		}
		if (chip->is_qc20_chg_enabled &&
				chip->qc20_ibatmax > 0 &&
				chip->qc20_ibatsafe > 0)
			rc = qpnp_chg_ibatsafe_set(chip, chip->qc20_ibatsafe);
		else
			rc = qpnp_chg_ibatsafe_set(chip, chip->safe_current);

		if (rc) {
			pr_err("failed setting ibat_Safe rc=%d\n", rc);
			return rc;
		}

		if (chip->tchg_mins != 0) {
		/* Extend safety timer as 16hr by monitor 8hrs timeout twice
		   if safety_time is more than 8.5hr */
			if (chip->tchg_mins > SAFETY_TIME_MAX_LIMIT) {
				rc = qpnp_chg_tchg_max_set(chip, SAFETY_TIME_8HR_TWICE);
			} else {
				rc = qpnp_chg_tchg_max_set(chip, chip->tchg_mins);
			}

			if (rc) {
				pr_err("Failed to set max time to %d minutes rc=%d\n",
					chip->tchg_mins, rc);
				return rc;
			}
		}

		/* HACK: Disable wdog */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x62,
			0xFF, 0xA0, 1);

		/* HACK: use analog EOC */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base +
			CHGR_IBAT_TERM_CHGR,
			0xFF, 0x08, 1);

		break;
	case SMBB_BUCK_SUBTYPE:
	case SMBBP_BUCK_SUBTYPE:
	case SMBCL_BUCK_SUBTYPE:
		rc = qpnp_chg_toggle_chg_done_logic(chip, 0);
		if (rc)
			return rc;

		rc = qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_BCK_VBAT_REG_MODE,
			BUCK_VBAT_REG_NODE_SEL_BIT,
			BUCK_VBAT_REG_NODE_SEL_BIT, 1);
		if (rc) {
			pr_err("failed to enable IR drop comp rc=%d\n", rc);
			return rc;
		}
		break;
	case SMBB_BAT_IF_SUBTYPE:
	case SMBBP_BAT_IF_SUBTYPE:
	case SMBCL_BAT_IF_SUBTYPE:
		/* Select battery presence detection */
		switch (chip->bpd_detection) {
		case BPD_TYPE_BAT_THM:
			reg = BAT_THM_EN;
			break;
		case BPD_TYPE_BAT_ID:
			reg = BAT_ID_EN;
			break;
		case BPD_TYPE_BAT_THM_BAT_ID:
			reg = BAT_THM_EN | BAT_ID_EN;
			break;
		default:
			reg = BAT_THM_EN;
			break;
		}

		if (!(chip->is_embeded_batt)) {
			rc = qpnp_chg_masked_write(chip,
				chip->bat_if_base + BAT_IF_BPD_CTRL,
				BAT_IF_BPD_CTRL_SEL,
				reg, 1);
			if (rc) {
				pr_err("failed to chose BPD rc=%d\n", rc);
				return rc;
			}
		} else {
			/*To disable pm8941 BPD due to abnormal batt-pres IRQ*/
			reg = 0x00;
			rc = qpnp_chg_write(chip, &reg,
				chip->bat_if_base + BAT_IF_BPD_CTRL, 1);

			if (rc) {
				pr_err("failed to disable BPD rc=%d\n", rc);
				return rc;
			}
			pr_info("Skip it due to embeded battery, present=%d\n",
					chip->is_embeded_batt);
		}

		/* Force on VREF_BAT_THM */
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc) {
			pr_err("failed to force on VREF_BAT_THM rc=%d\n", rc);
			return rc;
		}

		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);

		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			rdesc			= &(chip->batfet_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_batfet_vreg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->batfet_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->batfet_vreg.rdev)) {
				rc = PTR_ERR(chip->batfet_vreg.rdev);
				chip->batfet_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("batfet reg failed, rc=%d\n",
							rc);
				return rc;
			}
		}
		break;
	case SMBB_USB_CHGPTH_SUBTYPE:
	case SMBBP_USB_CHGPTH_SUBTYPE:
	case SMBCL_USB_CHGPTH_SUBTYPE:
		if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
				ENUM_T_STOP_BIT,
				ENUM_T_STOP_BIT, 1);
			if (rc) {
				pr_err("failed to write enum stop rc=%d\n", rc);
				return -ENXIO;
			}
		}

		init_data = of_get_regulator_init_data(chip->dev,
						       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"otg-parent-supply", NULL))
				init_data->supply_regulator = "otg-parent";

			rdesc			= &(chip->otg_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_otg_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->otg_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->otg_vreg.rdev)) {
				rc = PTR_ERR(chip->otg_vreg.rdev);
				chip->otg_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("OTG reg failed, rc=%d\n", rc);
				return rc;
			}
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OVP_CTL,
			USB_VALID_DEB_20MS,
			USB_VALID_DEB_20MS, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
			ENUM_T_STOP_BIT,
			ENUM_T_STOP_BIT, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_CHG_GONE_REV_BST,
			0xFF,
			0x80, 1);

		if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
			(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
#if !(defined(CONFIG_HTC_BATT_8960))
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + USB_OCP_THR,
				OCP_THR_MASK,
				OCP_THR_900_MA, 1);
#else
			/* we use 200_MA setting instead of 900_MA */
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + USB_OCP_THR,
				OCP_THR_MASK,
				OCP_THR_500_MA, 1);
#endif /* !CONFIG_HTC_BATT_8960 */
			if (rc)
				pr_err("Failed to configure OCP rc = %d\n", rc);
		}

		break;
	case SMBB_DC_CHGPTH_SUBTYPE:
		break;
	case SMBB_BOOST_SUBTYPE:
	case SMBBP_BOOST_SUBTYPE:
		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"boost-parent-supply", NULL))
				init_data->supply_regulator = "boost-parent";

			rdesc			= &(chip->boost_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_boost_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS
					| REGULATOR_CHANGE_VOLTAGE;

			chip->boost_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->boost_vreg.rdev)) {
				rc = PTR_ERR(chip->boost_vreg.rdev);
				chip->boost_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("boost reg failed, rc=%d\n", rc);
				return rc;
			}
		}
		break;
	case SMBB_MISC_SUBTYPE:
	case SMBBP_MISC_SUBTYPE:
	case SMBCL_MISC_SUBTYPE:
		if (subtype == SMBB_MISC_SUBTYPE)
			chip->type = SMBB;
		else if (subtype == SMBBP_MISC_SUBTYPE)
			chip->type = SMBBP;
		else if (subtype == SMBCL_MISC_SUBTYPE)
			chip->type = SMBCL;

		pr_debug("Setting BOOT_DONE\n");
		rc = qpnp_chg_masked_write(chip,
			chip->misc_base + CHGR_MISC_BOOT_DONE,
			CHGR_BOOT_DONE, CHGR_BOOT_DONE, 1);
		rc = qpnp_chg_read(chip, &reg,
				 chip->misc_base + MISC_REVISION2, 1);
		if (rc) {
			pr_err("failed to read revision register rc=%d\n", rc);
			return rc;
		}

		chip->revision = reg;
		break;
	default:
		pr_err("Invalid peripheral subtype\n");
	}
	return rc;
}

#define OF_PROP_READ(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval)							\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"qcom," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0;						\
	else if (retval)						\
		pr_err("Error reading " #qpnp_dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

#define OF_PROP_READ_HTC(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval) 						\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"htc," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0; 					\
	else if (retval)						\
		pr_err("Error reading htc," #qpnp_dt_property		\
				" property rc = %d\n", rc); 	\
} while (0)

static int
qpnp_charger_read_dt_props(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	const char *bpd;

	OF_PROP_READ(chip, max_voltage_mv, "vddmax-mv", rc, false);
	OF_PROP_READ(chip, min_voltage_mv, "vinmin-mv", rc, false);
	OF_PROP_READ(chip, safe_voltage_mv, "vddsafe-mv", rc, false);
	OF_PROP_READ(chip, resume_delta_mv, "vbatdet-delta-mv", rc, false);
	OF_PROP_READ(chip, safe_current, "ibatsafe-ma", rc, false);
	OF_PROP_READ(chip, max_bat_chg_current, "ibatmax-ma", rc, false);
	if (rc) {
		pr_err("failed to read required dt parameters %d\n", rc);
		return rc;
	}

	OF_PROP_READ(chip, term_current, "ibatterm-ma", rc, true);
	OF_PROP_READ(chip, maxinput_dc_ma, "maxinput-dc-ma", rc, true);
	OF_PROP_READ(chip, maxinput_usb_ma, "maxinput-usb-ma", rc, true);
	OF_PROP_READ(chip, warm_bat_decidegc, "warm-bat-decidegc", rc, true);
	OF_PROP_READ(chip, cool_bat_decidegc, "cool-bat-decidegc", rc, true);
	OF_PROP_READ(chip, tchg_mins, "tchg-mins", rc, true);
	OF_PROP_READ(chip, hot_batt_p, "batt-hot-percentage", rc, true);
	OF_PROP_READ(chip, cold_batt_p, "batt-cold-percentage", rc, true);
	OF_PROP_READ(chip, soc_resume_limit, "resume-soc", rc, true);
	OF_PROP_READ(chip, batt_weak_voltage_mv, "vbatweak-mv", rc, true);
	OF_PROP_READ(chip, eoc_ibat_thre_ma, "eoc-ibat-thre-ma", rc, true);
	OF_PROP_READ(chip, is_embeded_batt, "is-embeded-batt", rc, true);
	OF_PROP_READ(chip, regulate_vin_min_thr_mv, "regulate-vin-min-thr-mv", rc, true);
	OF_PROP_READ(chip, lower_vin_min, "lower-vin-min", rc, true);

	if (rc) {
		pr_err("failed to read required dt parameters %d\n", rc);
		return rc;
	}

	rc = of_property_read_string(chip->spmi->dev.of_node,
		"qcom,bpd-detection", &bpd);
	if (rc) {
		/* Select BAT_THM as default BPD scheme */
		chip->bpd_detection = BPD_TYPE_BAT_THM;
		rc = 0;
	} else {
		chip->bpd_detection = get_bpd(bpd);
		if (chip->bpd_detection < 0) {
			pr_err("failed to determine bpd schema %d\n", rc);
			return rc;
		}
	}

	/* Look up JEITA compliance parameters if cool and warm temp provided */
	if (chip->cool_bat_decidegc || chip->warm_bat_decidegc) {
		chip->adc_tm_dev = qpnp_get_adc_tm(chip->dev, "chg");
		if (IS_ERR(chip->adc_tm_dev)) {
			rc = PTR_ERR(chip->adc_tm_dev);
			if (rc != -EPROBE_DEFER)
				pr_err("adc-tm not ready, defer probe\n");
			return rc;
		}

		OF_PROP_READ(chip, warm_bat_chg_ma, "ibatmax-warm-ma", rc, true);
		OF_PROP_READ(chip, cool_bat_chg_ma, "ibatmax-cool-ma", rc, true);
		OF_PROP_READ(chip, warm_bat_mv, "warm-bat-mv", rc, true);
		OF_PROP_READ(chip, cool_bat_mv, "cool-bat-mv", rc, true);
		if (rc)
			return rc;
	}

	/* Get the btc-disabled property */
	chip->btc_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,btc-disabled");

	/* Get the charging-disabled property */
	chip->charging_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charging-disabled");

	/* Get the duty-cycle-100p property */
	chip->duty_cycle_100p = of_property_read_bool(
					chip->spmi->dev.of_node,
					"qcom,duty-cycle-100p");

	/* Get the fake-batt-values property */
	chip->use_default_batt_values =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,use-default-batt-values");

	/* Get AICL feature enabled property */
	chip->is_pm8921_aicl_enabled =
		of_property_read_bool(chip->spmi->dev.of_node,
				"qcom,pm8921-aicl-enabled");

	rc = of_property_read_u32(chip->spmi->dev.of_node,
					"qcom,pm8921-aicl-target-ma",
					&aicl_target_ma);
	if (!rc && aicl_target_ma)
		pr_info("set AICL max target value %d ma\n", aicl_target_ma);
	else
		aicl_target_ma = USB_MA_1500;

	chip->enable_qct_adjust_vddmax = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,enable-qct-adjust-vddmax");

	chip->is_qc20_chg_enabled =
		of_property_read_bool(chip->spmi->dev.of_node,
				"qcom,qc20-chg-enabled");

	/* Disable AICL and QC2.0 when limit the board version */
	OF_PROP_READ(chip, is_aicl_qc20_enable_board_version, "aicl-qc20-enable-board-version", rc, true);
	if(!rc && chip->is_aicl_qc20_enable_board_version) {
		if(of_machine_pcbid() != chip->is_aicl_qc20_enable_board_version) {
			pr_info("Do not enable aicl and qc2.0. pcbid=%d, enable on %d\n",
					of_machine_pcbid(), chip->is_aicl_qc20_enable_board_version);
			chip->is_qc20_chg_enabled = 0;
			chip->is_pm8921_aicl_enabled = 0;
		}
	}

	/* Disable charging when faking battery values */
	if (chip->use_default_batt_values)
		chip->charging_disabled = true;

	chip->power_stage_workaround_enable =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,power-stage-reduced");

	of_get_property(chip->spmi->dev.of_node, "qcom,thermal-mitigation",
		&(chip->thermal_levels));

	if (chip->thermal_levels > sizeof(int)) {
		chip->thermal_mitigation = devm_kzalloc(chip->dev,
			chip->thermal_levels,
			GFP_KERNEL);

		if (chip->thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return -ENOMEM;
		}

		chip->thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(chip->spmi->dev.of_node,
				"qcom,thermal-mitigation",
				chip->thermal_mitigation, chip->thermal_levels);
		if (rc) {
			pr_err("qcom,thermal-mitigation missing in dt\n");
			return rc;
		}
	}

	/* Get en_otg gpio*/
	chip->otg_en_gpio = of_get_named_gpio(chip->spmi->dev.of_node, "htc,otg-en-gpio", 0);
	if (!gpio_is_valid(chip->otg_en_gpio)) {
			pr_info("not found otg_en_gpio (%d) not enable this feature.", chip->otg_en_gpio);
	}

	/* Get external OVP gpio*/
	chip->ext_ovpfet_gpio = of_get_named_gpio(chip->spmi->dev.of_node, "htc,external-ovpfet-gpio", 0);
	if (!gpio_is_valid(chip->ext_ovpfet_gpio)) {
			pr_info("not found ext_ovpfet_gpio (%d) not enable this feature.", chip->ext_ovpfet_gpio);
			chip->ext_ovpfet_gpio = 0;
	} else {
		/* Get Rext in normal temperature */
		OF_PROP_READ_HTC(chip, ext_ovpfet_rext, "external-ovpfet-temp-ok-rext", rc, true);
		/* Get Rext in cool temperature */
		OF_PROP_READ_HTC(chip, ext_ovpfet_rext_cool, "external-ovpfet-temp-cool-rext", rc, true);
		/* Get cool temperature */
		OF_PROP_READ_HTC(chip, ext_ovpfet_temp_cool, "external-ovpfet-cool-decidegc", rc, true);
		pr_info("external ovpfet: gpio=%d, rext=%d, rext_cool=%d, temp_cool=%d\n",
					chip->ext_ovpfet_gpio, chip->ext_ovpfet_rext,
					chip->ext_ovpfet_rext_cool, chip->ext_ovpfet_temp_cool);
	}

	OF_PROP_READ_HTC(chip, ext_ovpfet_enable_board_version, "ext-ovpfet-enable-board-version", rc, true);
	if (chip->ext_ovpfet_enable_board_version) {
		if (of_machine_pcbid() < chip->ext_ovpfet_enable_board_version) {
			pr_info("not enable ovpfet pcbid (%d) < %d.",
						of_machine_pcbid(), chip->ext_ovpfet_enable_board_version);
			chip->ext_ovpfet_gpio = 0;
		}
	}

	return rc;
}

static int __devinit
qpnp_charger_probe(struct spmi_device *spmi)
{
	u8 subtype;
	struct qpnp_chg_chip	*chip;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	int rc = 0;

	chip = devm_kzalloc(&spmi->dev,
			sizeof(struct qpnp_chg_chip), GFP_KERNEL);
	if (chip == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}

	chip->prev_usb_max_ma = -EINVAL;
	chip->fake_battery_soc = -EINVAL;
	chip->dev = &(spmi->dev);
	chip->spmi = spmi;

#if !(defined(CONFIG_HTC_BATT_8960))
	chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		rc = -EPROBE_DEFER;
		goto fail_chg_enable;
	}
#endif /* !CONFIG_HTC_BATT_8960 */

	mutex_init(&chip->jeita_configure_lock);
	alarm_init(&chip->reduce_power_stage_alarm, ANDROID_ALARM_RTC_WAKEUP,
			qpnp_chg_reduce_power_stage_callback);
	INIT_WORK(&chip->reduce_power_stage_work,
			qpnp_chg_reduce_power_stage_work);

	/* Get all device tree properties */
	rc = qpnp_charger_read_dt_props(chip);
	if (rc)
		return rc;

	if (!chip->is_pm8921_aicl_enabled)
		usb_wall_threshold_ma = aicl_target_ma;
	else
		usb_wall_threshold_ma = USB_MA_500;

	/*
	 * Check if bat_if is set in DT and make sure VADC is present
	 * Also try loading the battery data profile if bat_if exists
	 */
	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
#ifdef CONFIG_ARCH_MSM8226
			/* FIXME: Need to check it !! Driver will init fail due to resource will return NULL */
			break;
#endif
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		if (subtype == SMBB_BAT_IF_SUBTYPE ||
			subtype == SMBBP_BAT_IF_SUBTYPE ||
			subtype == SMBCL_BAT_IF_SUBTYPE) {
			chip->vadc_dev = qpnp_get_vadc(chip->dev, "chg");
			if (IS_ERR(chip->vadc_dev)) {
				rc = PTR_ERR(chip->vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_err("vadc property missing\n");
				goto fail_chg_enable;
			}

			rc = qpnp_chg_load_battery_data(chip);
			if (rc)
				goto fail_chg_enable;
		}
	}

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
#ifdef CONFIG_ARCH_MSM8226
			/* FIXME: Need to check it !! Driver will init fail due to resource will return NULL */
			break;
#endif
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			chip->buck_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + SEC_ACCESS,
				0xFF,
				0xA5, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_VCHG_OV,
				0xff,
				0x00, 1);

			if (chip->duty_cycle_100p) {
				rc = qpnp_buck_set_100_duty_cycle_enable(chip,
						1);
				if (rc) {
					pr_err("failed to set duty cycle %d\n",
						rc);
					goto fail_chg_enable;
				}
			}

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->usb_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dc_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BOOST_SUBTYPE:
		case SMBBP_BOOST_SUBTYPE:
			chip->boost_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_MISC_SUBTYPE:
		case SMBBP_MISC_SUBTYPE:
		case SMBCL_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype=0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
			goto fail_chg_enable;
		}
	}
	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);

	the_chip = chip;

#if !(defined(CONFIG_HTC_BATT_8960))
	if (chip->bat_if_base) {
		chip->batt_psy.name = "battery";
		chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
		chip->batt_psy.properties = msm_batt_power_props;
		chip->batt_psy.num_properties =
			ARRAY_SIZE(msm_batt_power_props);
		chip->batt_psy.get_property = qpnp_batt_power_get_property;
		chip->batt_psy.set_property = qpnp_batt_power_set_property;
		chip->batt_psy.property_is_writeable =
				qpnp_batt_property_is_writeable;
		chip->batt_psy.external_power_changed =
				qpnp_batt_external_power_changed;
		chip->batt_psy.supplied_to = pm_batt_supplied_to;
		chip->batt_psy.num_supplicants =
				ARRAY_SIZE(pm_batt_supplied_to);

		rc = power_supply_register(chip->dev, &chip->batt_psy);
		if (rc < 0) {
			pr_err("batt failed to register rc = %d\n", rc);
			goto fail_chg_enable;
		}
		INIT_WORK(&chip->adc_measure_work,
			qpnp_bat_if_adc_measure_work);
		INIT_WORK(&chip->adc_disable_work,
			qpnp_bat_if_adc_disable_work);
	}
#endif /* !CONFIG_HTC_BATT_8960 */
	INIT_DELAYED_WORK(&chip->fix_reverse_boost_check_work,
		qpnp_fix_reverse_boost_check_work);
	INIT_DELAYED_WORK(&chip->eoc_work, qpnp_eoc_work);

	INIT_DELAYED_WORK(&chip->arb_stop_work, qpnp_arb_stop_work);

#if !(defined(CONFIG_HTC_BATT_8960))
	INIT_WORK(&chip->soc_check_work, qpnp_chg_soc_check_work);
	INIT_DELAYED_WORK(&chip->aicl_check_work, qpnp_aicl_check_work);
#else
	INIT_DELAYED_WORK(&chip->vin_collapse_check_work,
					vin_collapse_check_worker);
	INIT_DELAYED_WORK(&chip->resume_vddmax_configure_work,
					resume_vddmax_configure_work);
	INIT_DELAYED_WORK(&chip->readjust_vddmax_configure_work,
					readjust_vddmax_configure_work);
	INIT_DELAYED_WORK(&chip->aicl_check_work, aicl_check_worker);
	INIT_DELAYED_WORK(&chip->update_ovp_uvp_work, update_ovp_uvp_worker);
	chip->aicl_check_wq = create_singlethread_workqueue("aicl_check_wq");
	INIT_DELAYED_WORK(&chip->ovpfet_resistance_check_work, ovpfet_resistance_check_worker);

	wake_lock_init(&chip->vin_collapse_check_wake_lock,
			WAKE_LOCK_SUSPEND, "pm8921_vin_collapse_check_wake_lock");
	wake_lock_init(&chip->reverse_boost_wa_wake_lock,
		WAKE_LOCK_SUSPEND, "reverse_boost_wa_wake_lock");
#endif
	wake_lock_init(&chip->set_vbatdet_lock, WAKE_LOCK_SUSPEND,
			"batt_set_vbatdet_lock");


#if !(defined(CONFIG_HTC_BATT_8960))
	if (chip->dc_chgpth_base) {
		chip->dc_psy.name = "qpnp-dc";
		chip->dc_psy.type = POWER_SUPPLY_TYPE_MAINS;
		chip->dc_psy.supplied_to = pm_power_supplied_to;
		chip->dc_psy.num_supplicants = ARRAY_SIZE(pm_power_supplied_to);
		chip->dc_psy.properties = pm_power_props_mains;
		chip->dc_psy.num_properties = ARRAY_SIZE(pm_power_props_mains);
		chip->dc_psy.get_property = qpnp_power_get_property_mains;
		chip->dc_psy.set_property = qpnp_dc_power_set_property;
		chip->dc_psy.property_is_writeable =
				qpnp_dc_property_is_writeable;

		rc = power_supply_register(chip->dev, &chip->dc_psy);
		if (rc < 0) {
			pr_err("power_supply_register dc failed rc=%d\n", rc);
			goto unregister_batt;
		}
	}
#endif /* !CONFIG_HTC_BATT_8960 */

	/* Turn on appropriate workaround flags */
	rc = qpnp_chg_setup_flags(chip);
	if (rc < 0) {
		pr_err("failed to setup flags rc=%d\n", rc);
		goto unregister_dc_psy;
	}

	if (chip->maxinput_dc_ma && chip->dc_chgpth_base) {
		rc = qpnp_chg_idcmax_set(chip, chip->maxinput_dc_ma);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			goto unregister_dc_psy;
		}
	}

	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
							&& chip->bat_if_base) {
		chip->adc_param.low_temp = chip->cool_bat_decidegc;
		chip->adc_param.high_temp = chip->warm_bat_decidegc;
		chip->adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
		chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		chip->adc_param.btm_ctx = chip;
		chip->adc_param.threshold_notification =
						qpnp_chg_adc_notification;
		chip->adc_param.channel = LR_MUX1_BATT_THERM;

		if (get_prop_batt_present(chip)) {
			rc = qpnp_adc_tm_channel_measure(chip->adc_tm_dev,
							&chip->adc_param);
			if (rc) {
				pr_err("request ADC error %d\n", rc);
				goto unregister_dc_psy;
			}
		}
	}

	rc = qpnp_chg_bat_if_configure_btc(chip);
	if (rc) {
		pr_err("failed to configure btc %d\n", rc);
		goto unregister_dc_psy;
	}

#if !(defined(CONFIG_HTC_BATT_8960))
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	qpnp_chg_set_appropriate_vddmax(chip);
#endif /* !CONFIG_HTC_BATT_8960 */

	rc = qpnp_chg_request_irqs(chip);
	if (rc) {
		pr_err("failed to request interrupts %d\n", rc);
		goto unregister_dc_psy;
	}

#if !(defined(CONFIG_HTC_BATT_8960))
	qpnp_chg_usb_usbin_valid_irq_handler(chip->usbin_valid.irq, chip);
	qpnp_chg_dc_dcin_valid_irq_handler(chip->dcin_valid.irq, chip);
	power_supply_set_present(chip->usb_psy,
			qpnp_chg_is_usb_chg_plugged_in(chip));

	/* Set USB psy online to avoid userspace from shutting down if battery
	 * capacity is at zero and no chargers online. */
	if (qpnp_chg_is_usb_chg_plugged_in(chip))
		power_supply_set_online(chip->usb_psy, 1);
#else
	/* issue a fake coarse_det_usb irq to check ovp status */
	qpnp_chg_coarse_det_usb_irq_handler(chip->coarse_usb_det.irq, chip);
#endif /* !CONFIG_HTC_BATT_8960 */

	htc_charger_event_notify(HTC_CHARGER_EVENT_READY);

	qpnp_chg_read(chip, &pmic_rev, REG_PMIC_HWREV, 1);
#ifdef CONFIG_ARCH_MSM8974
	pr_info("pm8941 HW revision: 0x%x\n",pmic_rev);
#elif defined(CONFIG_ARCH_MSM8226)
	pr_info("pm8x26 HW revision: 0x%x\n",pmic_rev);
#endif

#if !(defined(CONFIG_HTC_BATT_8960))
	schedule_delayed_work(&chip->aicl_check_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
#endif

	pr_info("success chg_dis = %d, bpd = %d, usb = %d, dc = %d b_health = %d batt_present = %d\n",
			chip->charging_disabled,
			chip->bpd_detection,
			qpnp_chg_is_usb_chg_plugged_in(chip),
			qpnp_chg_is_dc_chg_plugged_in(chip),
			get_prop_batt_present(chip),
			get_prop_batt_health(chip));
	return 0;

unregister_dc_psy:
	if (chip->dc_chgpth_base)
		power_supply_unregister(&chip->dc_psy);
#if !(defined(CONFIG_HTC_BATT_8960))
unregister_batt:
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
#endif /* !CONFIG_HTC_BATT_8960 */

fail_chg_enable:
	pr_warn("pm8941 charger init failed\n");
	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);
	return rc;
}

static int __devexit
qpnp_charger_remove(struct spmi_device *spmi)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(&spmi->dev);
	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& chip->batt_present) {
		qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev,
							&chip->adc_param);
	}
	cancel_delayed_work_sync(&chip->aicl_check_work);
	power_supply_unregister(&chip->dc_psy);
	cancel_work_sync(&chip->soc_check_work);
	cancel_delayed_work_sync(&chip->arb_stop_work);
	cancel_delayed_work_sync(&chip->eoc_work);
	cancel_work_sync(&chip->adc_disable_work);
	cancel_work_sync(&chip->adc_measure_work);
	power_supply_unregister(&chip->batt_psy);
	/* FIXME: need to cancel batfet_lcl_work after sync ULPM related patches*/
	/*cancel_work_sync(&chip->batfet_lcl_work);*/
	cancel_work_sync(&chip->reduce_power_stage_work);
	alarm_cancel(&chip->reduce_power_stage_alarm);
	/* FIXME: need to cancel batfet_lcl_work after sync ULPM related patches*/
	/*mutex_destroy(&chip->batfet_vreg_lock);*/
	mutex_destroy(&chip->jeita_configure_lock);

	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);

	the_chip = NULL;

	return 0;
}

static int qpnp_chg_resume(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc)
			pr_err("failed to force on VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static int qpnp_chg_suspend(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc)
			pr_err("failed to set FSM VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static const struct dev_pm_ops qpnp_chg_pm_ops = {
	.resume		= qpnp_chg_resume,
	.suspend	= qpnp_chg_suspend,
};

static struct spmi_driver qpnp_charger_driver = {
	.probe		= qpnp_charger_probe,
	.remove		= __devexit_p(qpnp_charger_remove),
	.driver		= {
		.name		= QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_charger_match_table,
		.pm		= &qpnp_chg_pm_ops,
	},
};

/**
 * qpnp_chg_init() - register spmi driver for qpnp-chg
 */
int __init
qpnp_chg_init(void)
{
	flag_keep_charge_on =
		(get_kernel_flag() & KERNEL_FLAG_KEEP_CHARG_ON) ? 1 : 0;
	flag_pa_recharge =
		(get_kernel_flag() & KERNEL_FLAG_PA_RECHARG_TEST) ? 1 : 0;
	flag_enable_bms_charger_log =
               (get_kernel_flag() & KERNEL_FLAG_ENABLE_BMS_CHARGER_LOG) ? 1 : 0;
	return spmi_driver_register(&qpnp_charger_driver);
}
module_init(qpnp_chg_init);

static void __exit
qpnp_chg_exit(void)
{
	spmi_driver_unregister(&qpnp_charger_driver);
}
module_exit(qpnp_chg_exit);


MODULE_DESCRIPTION("QPNP charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_CHARGER_DEV_NAME);
