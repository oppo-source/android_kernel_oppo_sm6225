// SPDX-License-Identifier: GPL-2.0
#include <linux/irqreturn.h>
#include <trace/hooks/ufshcd.h>
#include "ufs-qcom.h"
#include "ufs-flastor-hid.h"

static int ufshid_create_sysfs(struct ufshid_flastor_dev *hid);

static inline int ufshid_flastor_get_state(struct ufshid_flastor_info *hid)
{
	return atomic_read(&hid->hid_state);
}

static inline void ufshid_flastor_set_state(struct ufshid_flastor_info *hid, int state)
{
	atomic_set(&hid->hid_state, state);
}

inline void ufs_fhid_rpm_put_noidle(struct ufs_hba *hba)
{
	pm_runtime_put_noidle(&hba->sdev_ufs_device->sdev_gendev);
}

static inline int ufshid_schedule_delayed_work(struct delayed_work *work,
					       unsigned long delay)
{
	return queue_delayed_work(system_freezable_wq, work, delay);
}

static inline int ufshid_is_not_present(struct ufshid_flastor_dev *hid)
{
	enum UFSHID_STATE_FLASTOR cur_state = ufshid_flastor_get_state(hid->hid_info);

	if (cur_state != HID_PRESENT_FLASTOR) {
		INFO_MSG_FLASTOR("hid_state != HID_PRESENT (%d)", cur_state);
		return -ENODEV;
	}
	return 0;
}

static int ufshid_read_attr(struct ufshid_flastor_dev *hid, u8 idn, u32 *attr_val)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	int ret = 0;

	ufshcd_rpm_get_sync(hba);

	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn, 0,
				      UFSHID_SELECTOR_FLASTOR, attr_val);
	if (ret) {
		ERR_MSG_FLASTOR("read attr [0x%.2X] fail. (%d)", idn, ret);
		goto err_out;
	}

	HID_DEBUG_FLASTOR(hid, "hid_attr read [0x%.2X] %u (0x%X)", idn, *attr_val,
		  *attr_val);
err_out:
	ufs_fhid_rpm_put_noidle(hba);

	return ret;
}

static int ufshid_set_flag(struct ufshid_flastor_dev *hid, u8 idn)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	bool flag_res;
	int ret = 0;

	ufshcd_rpm_get_sync(hba);

	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG, idn, 0, &flag_res);
	if (ret) {
		ERR_MSG_FLASTOR("set flag [0x%.2X] fail. (%d)", idn, ret);
		goto err_out;
	}

	HID_DEBUG_FLASTOR(hid, "hid_flag set [0x%.2X] %u", idn, flag_res);
err_out:
	ufs_fhid_rpm_put_noidle(hba);

	return ret;
}

static int ufshid_clear_flag(struct ufshid_flastor_dev *hid, u8 idn)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	bool flag_res;
	int ret = 0;

	ufshcd_rpm_get_sync(hba);

	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG, idn, 0, &flag_res);
	if (ret) {
		ERR_MSG_FLASTOR("clear flag [0x%.2X] fail. (%d)", idn, ret);
		goto err_out;
	}

	HID_DEBUG_FLASTOR(hid, "hid_flag clear [0x%.2X] %u", idn, flag_res);
err_out:
	ufs_fhid_rpm_put_noidle(hba);

	return ret;
}

static int ufs_read_desc(struct ufs_hba *hba, u8 desc_id, u8 desc_index,
			  u8 *desc_buf, u32 size)
{
	int err = 0;

	ufshcd_rpm_get_sync(hba);

	err = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					    desc_id, desc_index,
					    UFSHID_SELECTOR_FLASTOR,
					    desc_buf, &size);
	if (err)
		ERR_MSG_FLASTOR("reading Device Desc failed. err = %d", err);

	ufshcd_rpm_put_sync(hba);

	return err;
}

static int ufshid_flastor_get_dev_info(struct ufshid_flastor_info *hid_info)
{
	int ret = 0;
	u8 desc_buf[UFSF_QUERY_DESC_DEVICE_MAX_SIZE_FLASTOR];

	ret = ufs_read_desc(hid_info->hba, QUERY_DESC_IDN_DEVICE, 0, desc_buf,
				 UFSF_QUERY_DESC_DEVICE_MAX_SIZE_FLASTOR);
	if (ret)
		return ret;

	INFO_MSG_FLASTOR("device lu count %d", desc_buf[DEVICE_DESC_PARAM_NUM_LU]);

	INFO_MSG_FLASTOR("length=%u(0x%x) bSupport=0x%.2x, extend=0x%.2x_%.2x",
		  desc_buf[DEVICE_DESC_PARAM_LEN],
		  desc_buf[DEVICE_DESC_PARAM_LEN],
		  desc_buf[DEVICE_DESC_PARAM_UFS_FEAT],
		  desc_buf[DEVICE_DESC_PARAM_EX_FEAT_SUP_FLASTOR+2],
		  desc_buf[DEVICE_DESC_PARAM_EX_FEAT_SUP_FLASTOR+3]);

	hid_info->hid_dev = NULL;

	if (!(get_unaligned_be32(desc_buf + DEVICE_DESC_PARAM_EX_FEAT_SUP_FLASTOR) &
	      UFS_FEATURE_SUPPORT_HID_BIT_FLASTOR)) {
		INFO_MSG_FLASTOR("bUFSExFeaturesSupport: HID not support");
		ret = -EOPNOTSUPP;
		goto err_out;
	}

	INFO_MSG_FLASTOR("bUFSExFeaturesSupport:FLASTOR HID support");

	hid_info->hid_dev = kzalloc(sizeof(struct ufshid_flastor_dev), GFP_KERNEL);
	if (!hid_info->hid_dev) {
		ERR_MSG_FLASTOR("hid_dev memalloc fail");
		ret = -ENOMEM;
		goto err_out;
	}

	hid_info->hid_dev->hid_info = hid_info;

	return ret;
err_out:
	ufshid_flastor_set_state(hid_info, HID_FAILED_FLASTOR);
	return ret;
}

static inline int ufshid_issue_disable(struct ufshid_flastor_dev *hid)
{
	u8 idn = QUERY_ATTR_IDN_HID_OPERATION_FLASTOR;
	int ret = 0;

	ret = ufshid_clear_flag(hid, idn);

	return ret;
}

static int ufshid_get_attr_frag_level(struct ufshid_flastor_dev *hid, u32 *attr_val)
{
   int ret;

   ret = ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_FRAG_LEVEL_FLASTOR, attr_val);
   if (!ret)
	   HID_DEBUG_FLASTOR(hid, "Frag_lv %d", *attr_val & HID_FRAG_LEVEL_MASK_FLASTOR);

   return ret;
}

static int ufshid_get_attr_progress_ratio(struct ufshid_flastor_dev *hid, u32 *attr_val)
{
   int ret;

   ret = ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_STATE_FLASTOR, attr_val);

   if (!ret)
	   HID_DEBUG_FLASTOR(hid, "progress_ratio %d", *attr_val);

   return ret;
}

static bool ufshid_is_in_progress(struct ufshid_flastor_dev *hid)
{
	u32 state;

	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_STATE_FLASTOR, &state))
		return false;
	return state == HID_EXECUTING_FLASTOR;
}

void ufs_fhid_scsi_unblock_requests(struct ufs_hba *hba)
{
	if (atomic_dec_and_test(&hba->scsi_block_reqs_cnt))
		scsi_unblock_requests(hba->host);
}

void ufs_fhid_scsi_block_requests(struct ufs_hba *hba)
{
	if (atomic_inc_return(&hba->scsi_block_reqs_cnt) == 1)
		scsi_block_requests(hba->host);
}

int ufs_fhid_wait_for_doorbell_clr(struct ufs_hba *hba, u64 wait_timeout_us)
{
	unsigned long flags;
	int ret = 0;
	u32 tm_doorbell;
	u32 tr_doorbell;
	bool timeout = false, do_last_check = false;
	ktime_t start;

	ufshcd_hold(hba, false);
	spin_lock_irqsave(hba->host->host_lock, flags);
	/*
	 * Wait for all the outstanding tasks/transfer requests.
	 * Verify by checking the doorbell registers are clear.
	 */
	start = ktime_get();
	do {
		if (hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL) {
			ret = -EBUSY;
			goto out;
		}

		tm_doorbell = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);
		tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
		if (!tm_doorbell && !tr_doorbell) {
			timeout = false;
			break;
		} else if (do_last_check) {
			break;
		}

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		io_schedule_timeout(msecs_to_jiffies(20));
		if (ktime_to_us(ktime_sub(ktime_get(), start)) >
		    wait_timeout_us) {
			timeout = true;
			/*
			 * We might have scheduled out for long time so make
			 * sure to check if doorbells are cleared by this time
			 * or not.
			 */
			do_last_check = true;
		}
		spin_lock_irqsave(hba->host->host_lock, flags);
	} while (tm_doorbell || tr_doorbell);

	if (timeout) {
		dev_err(hba->dev,
			"%s: timedout waiting for doorbell to clear (tm=0x%x, tr=0x%x)\n",
			__func__, tm_doorbell, tr_doorbell);
		ret = -EBUSY;
	}
out:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_release(hba);
	return ret;
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static void ufshid_auto_hibern8_enable(struct ufshid_flastor_dev *hid,
				       unsigned int val)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	unsigned long flags;
	u32 reg;

	val = !!val;

	/* Update auto hibern8 timer value if supported */
	if (!ufshcd_is_auto_hibern8_supported(hba))
		return;

	ufshcd_rpm_get_sync(hba);
	ufshcd_hold(hba, false);
	ufs_fhid_scsi_block_requests(hba);
	/* wait for all the outstanding requests to finish */
	ufs_fhid_wait_for_doorbell_clr(hba, U64_MAX);
	spin_lock_irqsave(hba->host->host_lock, flags);

	reg = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
	INFO_MSG_FLASTOR("ahit-reg 0x%X", reg);

	if (val ^ (reg != 0)) {
		if (val) {
			hba->ahit = hid->ahit;
		} else {
			/*
			 * Store current ahit value.
			 * We don't know who set the ahit value to different
			 * from the initial value
			 */
			hid->ahit = reg;
			hba->ahit = 0;
		}

		ufshcd_writel(hba, hba->ahit, REG_AUTO_HIBERNATE_IDLE_TIMER);

		/* Make sure the timer gets applied before further operations */
		mb();

		INFO_MSG_FLASTOR("[Before] is_auto_enabled %d", hid->is_auto_enabled);
		hid->is_auto_enabled = val;

		reg = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
		INFO_MSG_FLASTOR("[After] is_auto_enabled %d ahit-reg 0x%X",
			 hid->is_auto_enabled, reg);
	} else {
		INFO_MSG_FLASTOR("is_auto_enabled %d. so it does not changed",
			 hid->is_auto_enabled);
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufs_fhid_scsi_unblock_requests(hba);
	ufshcd_release(hba);
	ufs_fhid_rpm_put_noidle(hba);
}

static void ufshid_block_enter_suspend(struct ufshid_flastor_dev *hid)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	struct device *dev = &hba->sdev_ufs_device->sdev_gendev;
	unsigned long flags;

#if defined(CONFIG_UFSHID_FLASTOR_POC)
	if (unlikely(hid->block_suspend))
		return;

	hid->block_suspend = true;
#endif
	ufshcd_rpm_get_sync(hba);
	ufshcd_hold(hba, false);

	spin_lock_irqsave(hba->host->host_lock, flags);
	HID_DEBUG_FLASTOR(hid,
		  "dev->power.usage_count %d hba->clk_gating.active_reqs %d",
		  atomic_read(&dev->power.usage_count),
		  hba->clk_gating.active_reqs);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

/*
 * If the return value is not err, pm_runtime_put_noidle() must be called once.
 * IMPORTANT : ufshid_hold_runtime_pm() & ufshid_release_runtime_pm() pair.
 */
static int ufshid_hold_runtime_pm(struct ufshid_flastor_dev *hid)
{
	struct ufs_hba *hba = hid->hid_info->hba;

	/* Case of system suspend */
	if (ufshid_flastor_get_state(hid->hid_info) == HID_SUSPEND_FLASTOR &&
	    !pm_runtime_suspended(&hba->sdev_ufs_device->sdev_gendev))
		return -ENODEV;

	/*
	 * After calling ufshcd_rpm_get_sync(),
	 * it is guaranteed that the wlun device is RPM_ACTIVE.
	 */
	ufshcd_rpm_get_sync(hba);

	/*
	 * Since HID resume is performed by a separate worker,
	 * it is sometimes judged to be in HID_SUSPEND state.
	 * Therefore, wait until ufshid_resume() changes the state
	 * of HID to HID_PRESENT.
	 */
	if (ufshid_flastor_get_state(hid->hid_info) == HID_SUSPEND_FLASTOR &&
	    !wait_for_completion_timeout(&hid->resume_compl,
					 WAIT_HID_RESUME_TIMEOUT_FLASTOR)) {
		WARN_MSG_FLASTOR("Waiting for HID resume times out");
		return -ETIMEDOUT;
	}

	if (ufshid_is_not_present(hid))
		return -ENODEV;

	return 0;
}

static inline void ufshid_release_runtime_pm(struct ufshid_flastor_dev *hid)
{
	struct ufs_hba *hba = hid->hid_info->hba;

	ufs_fhid_rpm_put_noidle(hba);
}

static void ufshid_allow_enter_suspend(struct ufshid_flastor_dev *hid)
{
	struct ufs_hba *hba = hid->hid_info->hba;
	struct device *dev = &hba->sdev_ufs_device->sdev_gendev;
	unsigned long flags;

#if defined(CONFIG_UFSHID_FLASTOR_POC)
	if (unlikely(!hid->block_suspend))
		return;

	hid->block_suspend = false;
#endif
	ufshcd_release(hba);
	ufs_fhid_rpm_put_noidle(hba);

	spin_lock_irqsave(hba->host->host_lock, flags);
	HID_DEBUG_FLASTOR(hid,
		  "dev->power.usage_count %d hba->clk_gating.active_reqs %d",
		  atomic_read(&dev->power.usage_count),
		  hba->clk_gating.active_reqs);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static int ufshid_trigger_off(struct ufshid_flastor_dev *hid)
	__must_hold(&hid->sysfs_lock)
{
	int ret;

	if (!hid->hid_trigger)
		return 0;

	ret = ufshid_hold_runtime_pm(hid);
	if (ret)
		return ret;

	hid->hid_trigger = false;
	HID_DEBUG_FLASTOR(hid, "hid_trigger 1 -> 0");

	ufshid_issue_disable(hid);

	ufshid_auto_hibern8_enable(hid, 1);

	ufshid_allow_enter_suspend(hid);

	ufshid_release_runtime_pm(hid);

	return 0;
}

static int ufshid_execute_query_op(struct ufshid_flastor_dev *hid)
{
	int ret;
	u8 idn = QUERY_ATTR_IDN_HID_OPERATION_FLASTOR;

	ret = ufshid_set_flag(hid, idn);

	return ret;
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static int ufshid_trigger_on(struct ufshid_flastor_dev *hid)
	__must_hold(&hid->sysfs_lock)
{
	int ret;

	if (hid->hid_trigger)
		return 0;

	ret = ufshid_hold_runtime_pm(hid);
	if (ret)
		return ret;

	hid->hid_trigger = true;
	HID_DEBUG_FLASTOR(hid, "trigger 0 -> 1");

	ufshid_block_enter_suspend(hid);

	ufshid_auto_hibern8_enable(hid, 0);

	ret = ufshid_execute_query_op(hid);
	if (ret) {
		ufshid_release_runtime_pm(hid);
		goto err_out;
	}

	ufshid_schedule_delayed_work(&hid->hid_trigger_work, 0);

	ufshid_release_runtime_pm(hid);

	return 0;

err_out:
	ret = ufshid_trigger_off(hid);
	if (unlikely(ret))
		ERR_MSG_FLASTOR("trigger off fail ret (%d)", ret);

	return ret;
}

static inline bool ufshid_check_progress_end(u32 val)
{
	return val == 3 || val == 0;
}

static void ufshid_trigger_work_fn(struct work_struct *dwork)
{
	struct ufshid_flastor_dev *hid;
	u32 attr_val;
	int ret;

	hid = container_of(dwork, struct ufshid_flastor_dev, hid_trigger_work.work);

	if (ufshid_is_not_present(hid))
		return;

	HID_DEBUG_FLASTOR(hid, "start hid_trigger_work_fn");

	mutex_lock(&hid->sysfs_lock);
	if (!hid->hid_trigger) {
		HID_DEBUG_FLASTOR(hid, "hid_trigger == false, return");
		goto finish_work;
	}

	if (ufshid_is_in_progress(hid)) {
		HID_DEBUG_FLASTOR(hid, "HID is in progress, so re-sched (%d ms)",
			  hid->hid_trigger_delay);
		goto resched;
	}

	ret = ufshid_get_attr_progress_ratio(hid, &attr_val);
	if (!ret && !ufshid_check_progress_end(attr_val)) {
		HID_DEBUG_FLASTOR(hid, "HID is on-going(state:%d), so re-sched (%d ms)",
				ret, hid->hid_trigger_delay);
		goto resched;
	}

	HID_DEBUG_FLASTOR(hid, "HID is ended or err (%d), so trigger off", ret);

	ufshid_issue_disable(hid);
	msleep(200);

	ret = ufshid_get_attr_frag_level(hid, &attr_val);
	if (ret)
		WARN_MSG_FLASTOR("get attr fragmented level fail.. (err %d)", ret);

	ret = ufshid_trigger_off(hid);
	if (ret)
		WARN_MSG_FLASTOR("trigger off fail.. must check it (err %d)", ret);

finish_work:
	mutex_unlock(&hid->sysfs_lock);

	return;

resched:
	mutex_unlock(&hid->sysfs_lock);

	ufshid_schedule_delayed_work(&hid->hid_trigger_work,
					 msecs_to_jiffies(hid->hid_trigger_delay));

	HID_DEBUG_FLASTOR(hid, "end hid_trigger_work_fn");
}

void ufshid_flastor_reset_host(struct ufshid_flastor_info *hid_info)
{
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;

	if (!hid)
		return;

	ufshid_flastor_set_state(hid_info, HID_RESET_FLASTOR);
	cancel_delayed_work_sync(&hid->hid_trigger_work);
}

void ufshid_flastor_reset(struct ufshid_flastor_info *hid_info)
{
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;

	if (!hid)
		return;

	ufshid_flastor_set_state(hid_info, HID_PRESENT_FLASTOR);

	/*
	 * hid_trigger will be checked under sysfs_lock in worker.
	 */
	if (hid->hid_trigger)
		ufshid_schedule_delayed_work(&hid->hid_trigger_work, 0);

	INFO_MSG_FLASTOR("reset completed.");
}

/*
 * called by ufshcd_vops_device_reset()
 */
inline void ufsf_fhid_reset_host(struct ufshid_flastor_info *hid_info)
{
	struct ufs_hba *hba = hid_info->hba;
	struct Scsi_Host *host = hba->host;
	unsigned long flags;
	u32 eh_flags;

	if (!hid_info->check_init)
		return;

	/*
	 * Check if it is error handling(eh) context.
	 *
	 * In the following cases, we can enter here even though it is not in eh
	 * context.
	 *  - when ufshcd_is_link_off() is true in ufshcd_resume()
	 *  - when ufshcd_vops_suspend() fails in ufshcd_suspend()
	 */
	spin_lock_irqsave(host->host_lock, flags);
	eh_flags = ufshcd_eh_in_progress_flastor(hba);
	spin_unlock_irqrestore(host->host_lock, flags);
	if (!eh_flags)
		return;

	INFO_MSG_FLASTOR("run reset_host.. hid_state(%d) -> HID_RESET",
		 ufshid_flastor_get_state(hid_info));
	if (ufshid_flastor_get_state(hid_info) == HID_PRESENT_FLASTOR)
		ufshid_flastor_reset_host(hid_info);

	schedule_work(&hid_info->reset_wait_work);
}

static inline void ufshid_remove_sysfs(struct ufshid_flastor_dev *hid)
{
	int ret;

	ret = kobject_uevent(&hid->kobj, KOBJ_REMOVE);
	INFO_MSG_FLASTOR("kobject removed (%d)", ret);
	kobject_del(&hid->kobj);
}

void ufshid_flastor_remove(struct ufshid_flastor_info *hid_info)
{
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;
	int ret;

	if (!hid)
		return;

	INFO_MSG_FLASTOR("start HID release");

	mutex_lock(&hid->sysfs_lock);

	ret = ufshid_trigger_off(hid);
	if (unlikely(ret))
		ERR_MSG_FLASTOR("trigger off fail ret (%d)", ret);

	ufshid_remove_sysfs(hid);

	ufshid_flastor_set_state(hid_info, HID_FAILED_FLASTOR);

	mutex_unlock(&hid->sysfs_lock);

	cancel_delayed_work_sync(&hid->hid_trigger_work);

	kfree(hid);

	INFO_MSG_FLASTOR("end HID release");
}

inline void ufsf_fhid_remove(struct ufshid_flastor_info *hid_info)
{
	if (ufshid_flastor_get_state(hid_info) == HID_PRESENT_FLASTOR)
		ufshid_flastor_remove(hid_info);
}

/*
 * worker to change the feature state to present after processing the error handler.
 */
static void ufshid_reset_wait_work_handler(struct work_struct *work)
{
	struct ufshid_flastor_info *hid_info;
	struct ufs_hba *hba;
	struct Scsi_Host *host;
	u32 ufshcd_state;
	unsigned long flags;

	hid_info = container_of(work, struct ufshid_flastor_info, reset_wait_work);
	hba = hid_info->hba;
	host = hba->host;

	/*
	 * Wait completion of hba->eh_work.
	 *
	 * reset_wait_work is scheduled at ufsf_reset_host(),
	 * so it can be waken up before eh_work is completed.
	 *
	 * ufsf_reset must be called after eh_work has completed.
	 */
	flush_work(&hba->eh_work);

	spin_lock_irqsave(host->host_lock, flags);
	ufshcd_state = hba->ufshcd_state;
	spin_unlock_irqrestore(host->host_lock, flags);

	if (ufshcd_state == UFSHCD_STATE_OPERATIONAL)
		ufshid_flastor_reset(hid_info);
}

static void ufshid_flastor_resume(struct ufshid_flastor_info          *hid_info, bool is_link_off)
{
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;

	if (!hid)
		return;

	ufshid_flastor_set_state(hid_info, HID_PRESENT_FLASTOR);

	complete(&hid->resume_compl);

	if (hid->hid_trigger)
		ufshid_schedule_delayed_work(&hid->hid_trigger_work,
				msecs_to_jiffies(hid->hid_trigger_delay));
}

static void ufshid_resume_work_handler(struct work_struct *work)
{
	struct ufshid_flastor_info *hid_info = container_of(work,
			struct ufshid_flastor_info, resume_work);
	struct ufs_hba *hba = hid_info->hba;
	bool is_link_off = ufshcd_is_link_off(hba);

	/*
	 * Resume of UFS feature should be called after power & link state
	 * are changed to active. Therefore, it is synchronized as follows.
	 *
	 * System PM: waits to acquire the semaphore used by ufshcd_wl_resume()
	 * Runtime PM: resume using ufshcd_rpm_get_sync()
	 */
	down(&hba->host_sem);
	ufshcd_rpm_get_sync(hba);

	if (ufshcd_is_ufs_dev_active(hba) && ufshcd_is_link_active(hba))
		if (ufshid_flastor_get_state(hid_info) == HID_SUSPEND_FLASTOR)
			ufshid_flastor_resume(hid_info, is_link_off);

	ufshcd_rpm_put(hba);
	up(&hba->host_sem);
}

void ufshid_flastor_init(struct ufshid_flastor_info *hid_info)
{
	int ret;
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;
	struct ufs_hba *hba = hid_info->hba;

	INFO_MSG_FLASTOR("HID_INIT_START");

	if (!hid) {
		ERR_MSG_FLASTOR("hid is not found. it is very weired. must check it");
		ufshid_flastor_set_state(hid_info, HID_FAILED_FLASTOR);
		return;
	}

	hid->hid_enable = false;
	hid->hid_trigger = false;
	hid->hid_trigger_delay = HID_TRIGGER_WORKER_DELAY_MS_DEFAULT_FLASTOR;
	INIT_DELAYED_WORK(&hid->hid_trigger_work, ufshid_trigger_work_fn);

	hid->hid_debug = false;
#if defined(CONFIG_UFSHID_FLASTOR_POC)
	hid->hid_debug = true;
	hid->block_suspend = false;
#endif

	/* If HCI supports auto hibern8, UFS Driver use it default */
	if (ufshcd_is_auto_hibern8_supported(hba))
		hid->is_auto_enabled = true;
	else
		hid->is_auto_enabled = false;

	/* Save default Auto-Hibernate Idle Timer register value */
	hid->ahit = hba->ahit;

	ret = ufshid_create_sysfs(hid);
	if (ret) {
		ERR_MSG_FLASTOR("sysfs init fail. so hid driver disabled");
		kfree(hid);
		ufshid_flastor_set_state(hid_info, HID_FAILED_FLASTOR);
		return;
	}
	INFO_MSG_FLASTOR("UFS HID create sysfs finished");

	ufshid_flastor_set_state(hid_info, HID_PRESENT_FLASTOR);
}

static void ufshid_device_check(struct ufs_hba *hba)
{
	struct ufs_qcom_host *ufs = ufshcd_get_variant(hba);
	struct ufshid_flastor_info *hid_info = &ufs->f_hid;

	if (ufshid_flastor_get_dev_info(hid_info))
		return;
}

static void ufshid_device_check_work_handler(struct work_struct *work)
{
	struct ufshid_flastor_info *hid_info =
		container_of(work, struct ufshid_flastor_info, device_check_work);

	if (hid_info->check_init)
		return;

	ufshid_device_check(hid_info->hba);

	if (ufshid_flastor_get_state(hid_info) == HID_NEED_INIT_FLASTOR)
		ufshid_flastor_init(hid_info);
	hid_info->check_init = true;
}

static inline bool ufsf_hid_is_valid_lun(int lun)
{
	return lun < UFS_UPIU_MAX_GENERAL_LUN_FLASTOR;
}

static inline void ufsf_hid_slave_configure(struct ufshid_flastor_info *hid_info,
				 struct scsi_device *sdev)
{
	if (!ufsf_hid_is_valid_lun(sdev->lun))
		return;

	hid_info->sdev_ufs_lu[sdev->lun] = sdev;
	INFO_MSG_FLASTOR("lun[%d] sdev(%p) q(%p)", (int)sdev->lun, sdev,
		 sdev->request_queue);

	if (!hid_info->check_init)
		schedule_work(&hid_info->device_check_work);
}

static void ufshid_vh_update_sdev(void *data, struct scsi_device *sdev)
{
	struct ufs_hba *hba = shost_priv(sdev->host);
	struct ufs_qcom_host *ufs = ufshcd_get_variant(hba);
	struct ufshid_flastor_info *hid_info = &ufs->f_hid;

	ufsf_hid_slave_configure(hid_info, sdev);
}

inline void ufsf_fhid_set_init_state(struct ufs_hba *hba)
{
	struct ufs_qcom_host *ufs = ufshcd_get_variant(hba);
	struct ufshid_flastor_info *hid_info = &ufs->f_hid;
	int ret = 0;

	ret = register_trace_android_vh_ufs_update_sdev(ufshid_vh_update_sdev, NULL);
	if (ret) {
		ERR_MSG_FLASTOR("register_trace_android_vh_ufs_update_sdev failed! ret = %d.", ret);
		return;
	}
	hid_info->hba = hba;

	INIT_WORK(&hid_info->device_check_work, ufshid_device_check_work_handler);
	INIT_WORK(&hid_info->reset_wait_work, ufshid_reset_wait_work_handler);
	INIT_WORK(&hid_info->resume_work, ufshid_resume_work_handler);

	ufshid_flastor_set_state(hid_info, HID_NEED_INIT_FLASTOR);
}

#define SPM_ACTIVE_POWER_LEVEL			1
void ufshid_flastor_suspend(struct ufshid_flastor_info        *hid_info, bool is_system_pm)
{
	struct ufshid_flastor_dev *hid = hid_info->hid_dev;
	struct ufs_hba *hba = NULL;
	int ret;

	if (!hid)
		return;

	if (!hid->hid_trigger)
		goto out;

	if (is_system_pm) {
		hba = hid->hid_info->hba;
		if (hba->spm_lvl <= SPM_ACTIVE_POWER_LEVEL) {
			if (ufshid_is_in_progress(hid))
				HID_DEBUG_FLASTOR(hid, "HID is in progress");
		} else {
			HID_DEBUG_FLASTOR(hid, "SPM Level is not 0 or 1. So HID will be off");
			ret = ufshid_trigger_off(hid);
			if (unlikely(ret))
				ERR_MSG_FLASTOR("trigger off fail ret (%d)", ret);
		}
	} else {
		ERR_MSG_FLASTOR("hid_trigger was set to block the runtime suspend. so weird");
	}

out:
	ufshid_flastor_set_state(hid_info, HID_SUSPEND_FLASTOR);

	init_completion(&hid->resume_compl);

	cancel_delayed_work_sync(&hid->hid_trigger_work);
}

inline void ufsf_fhid_suspend(struct ufshid_flastor_info *hid_info, bool is_system_pm)
{
	/*
	 * Wait completion of reset_wait_work.
	 *
	 * When suspend occurrs immediately after reset
	 * and reset_wait_work is executed late,
	 * we can enter here before ufsf_reset() cleans up the feature's reset sequence.
	 */
	flush_work(&hid_info->reset_wait_work);

	if (ufshid_flastor_get_state(hid_info) == HID_PRESENT_FLASTOR)
		ufshid_flastor_suspend(hid_info, is_system_pm);
}

/* sysfs function */
static ssize_t ufshid_sysfs_show_version(struct ufshid_flastor_dev *hid, char *buf)
{
	int ret;

	INFO_MSG_FLASTOR("FLASTOR HID version (%.4X)", HID_VERSION_FLASTOR);
	ret = snprintf(buf, PAGE_SIZE,
		"FLASTOR HID version (%.4X)\n", HID_VERSION_FLASTOR);

	return ret;
}

static ssize_t ufshid_sysfs_show_enable(struct ufshid_flastor_dev *hid, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_enable);
}

static ssize_t ufshid_sysfs_store_enable(struct ufshid_flastor_dev *hid, const char *buf,
					  size_t count)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (val)
		INFO_MSG_FLASTOR("Enable HID Feature");
	else
		INFO_MSG_FLASTOR("Disable HID Feature");

	if (val == hid->hid_enable)
		return count;

	hid->hid_enable = val;
	return count;
}

static ssize_t ufshid_sysfs_show_trigger(struct ufshid_flastor_dev *hid, char *buf)
{
	INFO_MSG_FLASTOR("hid_trigger %d", hid->hid_trigger);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_trigger);
}

static ssize_t ufshid_sysfs_store_trigger(struct ufshid_flastor_dev *hid,
					  const char *buf, size_t count)
{
	bool val;
	ssize_t ret;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (!hid->hid_enable) {
		INFO_MSG_FLASTOR("trigger fail as HID is disable");
		return -EOPNOTSUPP;
	}

	INFO_MSG_FLASTOR("HID_trigger %d", val);

	if (val == hid->hid_trigger)
		return count;

	if (val)
		ret = ufshid_trigger_on(hid);
	else
		ret = ufshid_trigger_off(hid);

	if (ret) {
		INFO_MSG_FLASTOR("Changing trigger val %d is fail (%ld)", val, ret);
		return ret;
	}

	return count;
}

static ssize_t ufshid_sysfs_show_trigger_interval(struct ufshid_flastor_dev *hid,
						  char *buf)
{
	INFO_MSG_FLASTOR("hid_trigger_interval %d", hid->hid_trigger_delay);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_trigger_delay);
}

static ssize_t ufshid_sysfs_store_trigger_interval(struct ufshid_flastor_dev *hid,
						   const char *buf,
						   size_t count)
{
	unsigned int val;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val < HID_TRIGGER_WORKER_DELAY_MS_MIN_FLASTOR ||
	    val > HID_TRIGGER_WORKER_DELAY_MS_MAX_FLASTOR) {
		INFO_MSG_FLASTOR("hid_trigger_interval (min) %4dms ~ (max) %4dms",
			 HID_TRIGGER_WORKER_DELAY_MS_MIN_FLASTOR,
			 HID_TRIGGER_WORKER_DELAY_MS_MAX_FLASTOR);
		return -EINVAL;
	}

	hid->hid_trigger_delay = val;
	INFO_MSG_FLASTOR("hid_trigger_interval %d", hid->hid_trigger_delay);

	return count;
}

static ssize_t ufshid_sysfs_show_debug(struct ufshid_flastor_dev *hid, char *buf)
{
	INFO_MSG_FLASTOR("debug %d", hid->hid_debug);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_debug);
}

static ssize_t ufshid_sysfs_store_debug(struct ufshid_flastor_dev *hid, const char *buf,
					size_t count)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	hid->hid_debug = val;

	INFO_MSG_FLASTOR("debug %d", hid->hid_debug);

	return count;
}

static ssize_t ufshid_sysfs_show_color(struct ufshid_flastor_dev *hid, char *buf)
{
	u32 attr_val;
	int frag_level;
	int ret;

	if (hid->hid_trigger) {
		INFO_MSG_FLASTOR("HID is in progress...");
		return snprintf(buf, PAGE_SIZE, "UNKNOWN: HID is in progress\n");
	}

	if (ufshid_get_attr_frag_level(hid, &attr_val))
		return -EINVAL;

	frag_level = attr_val & HID_FRAG_LEVEL_MASK_FLASTOR;
	ret = snprintf(buf, PAGE_SIZE, "%s\n", frag_level == HID_LEV_RED_FLASTOR ? "RED" :
		  frag_level == HID_LEV_YELLOW_FLASTOR ? "YELLOW" :
		  frag_level == HID_LEV_GREEN_FLASTOR ? "GREEN" :
		  frag_level == HID_LEV_GRAY_FLASTOR ? "GRAY" : "UNKNOWN");

	return ret;
}

#if defined(CONFIG_UFSHID_FLASTOR_POC)
static ssize_t ufshid_sysfs_show_hid_state(struct ufshid_flastor_dev *hid, char *buf)
{
	static const char *const states[] = {
		"IDLE",
		"Executing",
		"Interrupted",
		"Done"
	};

	u32 attr_val;
	const char *state = NULL;

	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_STATE_FLASTOR, &attr_val))
		return -EINVAL;

	if (attr_val >= HID_NUM_DEV_STATES_FLASTOR)
		return -EINVAL;

	state = states[attr_val];

	INFO_MSG_FLASTOR("hid_state %s", state);

	return snprintf(buf, PAGE_SIZE, "%s\n", state);
}

static ssize_t ufshid_sysfs_show_block_suspend(struct ufshid_flastor_dev *hid,
					       char *buf)
{
	INFO_MSG_FLASTOR("block suspend %d", hid->block_suspend);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->block_suspend);
}

static ssize_t ufshid_sysfs_store_block_suspend(struct ufshid_flastor_dev *hid,
						const char *buf, size_t count)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	INFO_MSG_FLASTOR("HID_block_suspend %d", val);

	if (val == hid->block_suspend)
		return count;

	if (val)
		ufshid_block_enter_suspend(hid);
	else
		ufshid_allow_enter_suspend(hid);

	hid->block_suspend = val ? true : false;

	INFO_MSG_FLASTOR("block suspend %d", hid->block_suspend);

	return count;
}

static ssize_t ufshid_sysfs_show_auto_hibern8_enable(struct ufshid_flastor_dev *hid,
						     char *buf)
{
	INFO_MSG_FLASTOR("HCI auto hibern8 %d", hid->is_auto_enabled);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->is_auto_enabled);
}

static ssize_t ufshid_sysfs_store_auto_hibern8_enable(struct ufshid_flastor_dev *hid,
						      const char *buf,
						      size_t count)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	ufshid_auto_hibern8_enable(hid, val);

	INFO_MSG_FLASTOR("HCI auto hibern8 %d", hid->is_auto_enabled);

	return count;
}
#endif

/* SYSFS DEFINE */
#define define_sysfs_ro(_name) __ATTR(_name, 0444,			\
				      ufshid_sysfs_show_##_name, NULL)
#define define_sysfs_rw(_name) __ATTR(_name, 0644,			\
				      ufshid_sysfs_show_##_name,	\
				      ufshid_sysfs_store_##_name)

static struct ufshid_flastor_sysfs_entry ufshid_sysfs_entries[] = {
	define_sysfs_ro(version),
	define_sysfs_rw(enable),
	define_sysfs_ro(color),
	define_sysfs_rw(trigger),
	define_sysfs_rw(trigger_interval),

	/* enable/disable debug log, default off */
	define_sysfs_rw(debug),
#if defined(CONFIG_UFSHID_FLASTOR_POC)
	define_sysfs_ro(hid_state),
	/* Attribute (RAW) */
	define_sysfs_rw(block_suspend),
	define_sysfs_rw(auto_hibern8_enable),
#endif
	__ATTR_NULL
};

static ssize_t ufshid_attr_show(struct kobject *kobj, struct attribute *attr,
				char *page)
{
	struct ufshid_flastor_sysfs_entry *entry;
	struct ufshid_flastor_dev *hid;
	ssize_t error;

	entry = container_of(attr, struct ufshid_flastor_sysfs_entry, attr);
	if (!entry->show)
		return -EIO;

	hid = container_of(kobj, struct ufshid_flastor_dev, kobj);
	error = ufshid_hold_runtime_pm(hid);
	if (error)
		return error;

	mutex_lock(&hid->sysfs_lock);
	error = entry->show(hid, page);
	mutex_unlock(&hid->sysfs_lock);

	ufshid_release_runtime_pm(hid);
	return error;
}

static ssize_t ufshid_attr_store(struct kobject *kobj, struct attribute *attr,
				 const char *page, size_t length)
{
	struct ufshid_flastor_sysfs_entry *entry;
	struct ufshid_flastor_dev *hid;
	ssize_t error;

	entry = container_of(attr, struct ufshid_flastor_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	hid = container_of(kobj, struct ufshid_flastor_dev, kobj);
	error = ufshid_hold_runtime_pm(hid);
	if (error)
		return error;

	mutex_lock(&hid->sysfs_lock);
	error = entry->store(hid, page, length);
	mutex_unlock(&hid->sysfs_lock);

	ufshid_release_runtime_pm(hid);
	return error;
}

static const struct sysfs_ops ufshid_sysfs_ops = {
	.show = ufshid_attr_show,
	.store = ufshid_attr_store,
};

static struct kobj_type ufshid_ktype = {
	.sysfs_ops = &ufshid_sysfs_ops,
	.release = NULL,
};

static int ufshid_create_sysfs(struct ufshid_flastor_dev *hid)
{
	struct device *dev = hid->hid_info->hba->dev;
	struct ufshid_flastor_sysfs_entry *entry;
	int err;

	hid->sysfs_entries = ufshid_sysfs_entries;

	kobject_init(&hid->kobj, &ufshid_ktype);
	mutex_init(&hid->sysfs_lock);

	INFO_MSG_FLASTOR("MID:0x%x, ufshid creates sysfs ufshid %p dev->kobj %p",
		 UFS_VENDOR_FLASTOR, &hid->kobj, &dev->kobj);

	err = kobject_add(&hid->kobj, kobject_get(&dev->kobj), "ufshid");
	if (!err) {
		for (entry = hid->sysfs_entries; entry->attr.name != NULL;
			 entry++) {
			INFO_MSG_FLASTOR("MID:0x%x, ufshid sysfs attr creates: %s",
				 UFS_VENDOR_FLASTOR, entry->attr.name);
			err = sysfs_create_file(&hid->kobj, &entry->attr);
			if (err) {
				ERR_MSG_FLASTOR("create entry(%s) failed",
					entry->attr.name);
				goto kobj_del;
			}
		}
		kobject_uevent(&hid->kobj, KOBJ_ADD);
	} else {
		ERR_MSG_FLASTOR("kobject_add failed");
	}

	return err;
kobj_del:
	err = kobject_uevent(&hid->kobj, KOBJ_REMOVE);
	INFO_MSG_FLASTOR("MID:0x%x, kobject removed (%d)", UFS_VENDOR_FLASTOR, err);
	kobject_del(&hid->kobj);

	return -EINVAL;
}

MODULE_LICENSE("GPL v2");
