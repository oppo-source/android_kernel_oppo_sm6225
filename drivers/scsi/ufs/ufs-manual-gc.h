#ifndef _UFS_MANUAL_GC_H_
#define _UFS_MANUAL_GC_H_

/* manual gc */
struct ufs_manual_gc {
	int state;
	bool hagc_support;
	struct hrtimer hrtimer;
	unsigned long delay_ms;
	struct work_struct hibern8_work;
	struct workqueue_struct *mgc_workq;
};

#define UFSHCD_MANUAL_GC_HOLD_HIBERN8		2000	/* 2 seconds */

#define QUERY_ATTR_IDN_MANUAL_GC_CONT		0x12
#define QUERY_ATTR_IDN_MANUAL_GC_STATUS		0x13

enum {
	MANUAL_GC_OFF = 0,
	MANUAL_GC_ON,
	MANUAL_GC_DISABLE,
	MANUAL_GC_ENABLE,
	MANUAL_GC_MAX,
};

extern void init_manual_gc(struct ufs_hba *hba);
extern int init_manual_gc_sysfs(struct ufs_hba *hba);
#endif
