/*
 * Copyright (C) 1994-2017 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
#ifndef	_PBS_SCHED_H
#define	_PBS_SCHED_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "attribute.h"
#include "sched_cmds.h"

#define PBS_SCHED_CYCLE_LEN_DEFAULT 1200

#define SC_STATUS_LEN 	10

/*attributes for the server's sched object*/
enum sched_atr {
	SCHED_ATR_SchedHost,
	SCHED_ATR_version,
	SCHED_ATR_sched_cycle_len,
	SCHED_ATR_dont_span_psets,
	SCHED_ATR_only_explicit_psets,
	SCHED_ATR_sched_preempt_enforce_resumption,
	SCHED_ATR_preempt_targets_enable,
	SCHED_ATR_job_sort_formula_threshold,
	SCHED_ATR_throughput_mode,
	SCHED_ATR_opt_backfill_fuzzy,
	SCHED_ATR_sched_port,
	SCHED_ATR_partition,
	SCHED_ATR_sched_priv,
	SCHED_ATR_sched_log,
	SCHED_ATR_scheduling,
	SCHED_ATR_schediteration,
	SCHED_ATR_sched_user,
	SCHED_ATR_sched_comment,
	SCHED_ATR_sched_state,
#include "site_sched_attr_enum.h"
	/* This must be last */
	SCHED_ATR_LAST
};
extern attribute_def sched_attr_def[];

typedef struct pbs_sched {
	pbs_list_link	sc_link;		/* forward/backward links */

	int scheduler_sock;
	int scheduler_sock2;
	int svr_do_schedule;
	int svr_do_sched_high;
	char sc_name[PBS_MAXSCHEDNAME];
	/* sched object's attributes  */
	attribute sch_attr[SCHED_ATR_LAST];
} pbs_sched;


extern pbs_sched *dflt_scheduler;
extern	pbs_list_head	svr_allscheds;

#ifdef	__cplusplus
}
#endif
#endif	/* _PBS_SCHED_H */
