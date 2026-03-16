// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#if !defined(_TRACE_SMP2P_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SMP2P_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sensor


TRACE_EVENT(sensor_wakeup_stat,

	TP_PROTO(int sensor_type),

	TP_ARGS(sensor_type),

	TP_STRUCT__entry(
		__field(	int,	sensor_type)
	),

	TP_fast_assign(
		__entry->sensor_type = sensor_type;
	),

	TP_printk("sensor_type:%d", __entry->sensor_type)
);


#endif // _TRACE_SMP2P_H

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_smp2p

/* This part must be outside protection */
#include <trace/define_trace.h>

