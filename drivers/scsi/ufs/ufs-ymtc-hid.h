/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _UFS_YMTC_HID_H_
#define _UFS_YMTC_HID_H_

#include <asm/unaligned.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/blktrace_api.h>
#include <linux/blkdev.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <scsi/scsi_cmnd.h>

#include "../../../block/blk.h"

#define UFS_VENDOR_YMTC		0xA9B

#define HID_VERSION_YMTC	0x0100

#define UFSHID_SELECTOR_YMTC			0x00

#define HID_TRIGGER_WORKER_DELAY_MS_DEFAULT_YMTC	2000
#define HID_TRIGGER_WORKER_DELAY_MS_MIN_YMTC		100
#define HID_TRIGGER_WORKER_DELAY_MS_MAX_YMTC		10000

#define HID_FRAG_LEVEL_MASK_YMTC		0xF

#define RESULT_NOT_DEFRAG_REQUIRED_YMTC		0

#define WAIT_HID_RESUME_TIMEOUT_YMTC			(2 * HZ)

#define QUERY_FLAG_IDN_HID_ENABLE_YMTC			0x13

#define UFS_UPIU_MAX_GENERAL_LUN_YMTC		32

/* Description */
#define UFSF_QUERY_DESC_UNIT_MAX_SIZE_YMTC		0x2D

/* Attribute idn for YMTC */
#define QUERY_ATTR_IDN_HID_RUNNING_STATUS_YMTC		0x30
#define QUERY_ATTR_IDN_HID_FRAG_LEVEL_YMTC		0x31
#define QUERY_ATTR_IDN_HID_FREQUENCY_YMTC		0x32
#define QUERY_ATTR_IDN_HID_STATUS_YMTC			0x33
#define QUERY_ATTR_IDN_HID_TOTAL_COUNT_YMTC		0x34

#define INFO_MSG_YMTC(msg, args...)		pr_info("%s:%d info: " msg "\n", \
					       __func__, __LINE__, ##args)
#define ERR_MSG_YMTC(msg, args...)		pr_err("%s:%d err: " msg "\n", \
					       __func__, __LINE__, ##args)
#define WARN_MSG_YMTC(msg, args...)		pr_warn("%s:%d warn: " msg "\n", \
					       __func__, __LINE__, ##args)
#define HID_DEBUG_YMTC(hid, msg, args...)					\
	do { if (hid->hid_debug)					\
		pr_err("%40s:%3d [%01d%02d%02d] " msg "\n",		\
		       __func__, __LINE__,				\
		       hid->hid_trigger,				\
		       atomic_read(&hid->hid_info->hba->dev->power.usage_count),\
		       hid->hid_info->hba->clk_gating.active_reqs, ##args);	\
	} while (0)

/* UFSHCD error handling flags */
enum {
	UFSHCD_EH_IN_PROGRESS_YMTC = (1 << 0),	/* ufshcd.c */
};
#define ufshcd_eh_in_progress_ymtc(h) \
	((h)->eh_flags & UFSHCD_EH_IN_PROGRESS_YMTC)		/* ufshcd.c */

enum {
	HID_LEV_NOT_NEED_YMTC		= 0,
	HID_LEV_NEED_YMTC		= 1,
	HID_LEV_URGENT_NEED_YMTC	= 2
};

enum UFSHID_STATE_YMTC {
	HID_NEED_INIT_YMTC	= 0,
	HID_PRESENT_YMTC	= 1,
	HID_SUSPEND_YMTC	= 2,
	HID_FAILED_YMTC		= -2,
	HID_RESET_YMTC		= -3,
};

enum UFSHID_DEV_STATE_YMTC {
	HID_IDLE_YMTC			= 0x0,
	HID_IN_PROGRESS_YMTC		= 0x1,
	HID_STOPPED_PREMATURELY_YMTC	= 0x2,
	HID_COMPLETED_SUCCESSFULLY_YMTC	= 0x3,
	HID_FAILED_BUSY_YMTC		= 0x4,
	HID_GENERAL_FAIL_YMTC		= 0x5,
	HID_NUM_DEV_STATES_YMTC		= 0x6
};

enum {
	HID_LEV_GRAY_YMTC	= 0,
	HID_LEV_GREEN_YMTC	= 1,
	HID_LEV_YELLOW_YMTC	= 2,
	HID_LEV_RED_YMTC	= 3,
	HID_LEV_UNKNOWN_YMTC	= 4,
};

struct ufshid_ymtc_info {
	struct ufs_hba *hba;
	struct scsi_device *sdev_ufs_lu[UFS_UPIU_MAX_GENERAL_LUN_YMTC];
	bool check_init;
	struct work_struct device_check_work;

	struct work_struct reset_wait_work;
	struct work_struct resume_work;
	atomic_t hid_state;
	struct ufshid_ymtc_dev *hid_dev;
};

struct ufshid_ymtc_dev {
	struct ufshid_ymtc_info *hid_info;

	bool hid_enable;
	unsigned int hid_trigger;   /* default value is false */
	struct delayed_work hid_trigger_work;
	unsigned int hid_trigger_delay;

	u32 ahit;			/* to restore ahit value */
	bool is_auto_enabled;

	/* for sysfs */
	struct kobject kobj;
	struct mutex sysfs_lock;
	struct ufshid_ymtc_sysfs_entry *sysfs_entries;

	/* for debug */
	bool hid_debug;
#if defined(CONFIG_UFSHID_YMTC_POC)
	bool block_suspend;
#endif
	struct completion resume_compl;
};

struct ufshid_ymtc_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct ufshid_ymtc_dev *hid, char *buf);
	ssize_t (*store)(struct ufshid_ymtc_dev *hid, const char *buf, size_t count);
};

void ufsf_yhid_remove(struct ufshid_ymtc_info *hid_info);
void ufsf_yhid_reset_host(struct ufshid_ymtc_info *hid_info);
void ufsf_yhid_set_init_state(struct ufs_hba *hba);
void ufsf_yhid_suspend(struct ufshid_ymtc_info *hid_info, bool is_system_pm);
#endif /* End of Header */
