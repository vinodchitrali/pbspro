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

/**
 * @file    sched_func.c
 *
 *@brief
 * 		sched_func.c - various functions dealing with schedulers
 *
 */
#include <errno.h>
#include <string.h>
#include <memory.h>
#include <pbs_config.h>
#include "pbs_share.h"
#include "pbs_sched.h"
#include "log.h"
#include "pbs_ifl.h"
#include "pbs_db.h"
#include "pbs_error.h"
#include "sched_cmds.h"
#include "server.h"

extern pbs_db_conn_t *svr_db_conn;

/**
 * @brief
 * 		sched_alloc - allocate space for a pbs_sched structure and
 * 		initialize attributes to "unset" and pbs_sched object is added
 * 		to svr_allscheds list
 *
 * @param[in]	sched_name	- scheduler  name
 *
 * @return	pbs_sched *
 * @retval	null	- space not available.
 */
pbs_sched *
sched_alloc(char *sched_name)
{
	int i;
	pbs_sched *psched;

	psched = calloc(1, sizeof(pbs_sched));

	if (psched == (pbs_sched *) 0) {
		log_err(errno, __func__, "Unable to allocate memory (malloc error)");
		return NULL;
	}

	CLEAR_LINK(psched->sc_link);
	strncpy(psched->sc_name, sched_name, PBS_MAXSCHEDNAME);
	psched->sc_name[PBS_MAXSCHEDNAME] = '\0';
	psched->svr_do_schedule = SCH_SCHEDULE_NULL;
	psched->svr_do_sched_high = SCH_SCHEDULE_NULL;
	psched->scheduler_sock = -1;
	psched->scheduler_sock2 = -1;
	append_link(&svr_allscheds, &psched->sc_link, psched);

	/* set the working attributes to "unspecified" */

	for (i = 0; i < (int) SCHED_ATR_LAST; i++) {
		clear_attr(&psched->sch_attr[i], &sched_attr_def[i]);
	}

	return (psched);
}

/**
 * @brief find a scheduler
 *
 * @param[in]	sched_name - scheduler name
 *
 * @return	pbs_sched *
 */

pbs_sched *
find_scheduler(char *sched_name)
{
	char *pc;
	pbs_sched *psched = NULL;
	if (!sched_name)
		return NULL;
	psched = (pbs_sched *) GET_NEXT(svr_allscheds);
	while (psched != (pbs_sched *) 0) {
		if (strcmp(sched_name, psched->sc_name) == 0)
			break;
		psched = (pbs_sched *) GET_NEXT(psched->sc_link);
	}
	return (psched);
}

/**
 * @brief free sched structure
 *
 * @param[in]	psched	- The pointer to the sched to free
 *
 */
void
sched_free(pbs_sched *psched)
{
	int i;
	attribute *pattr;
	attribute_def *pdef;

	/* remove any malloc working attribute space */

	for (i = 0; i < (int) SCHED_ATR_LAST; i++) {
		pdef = &sched_attr_def[i];
		pattr = &psched->sch_attr[i];

		pdef->at_free(pattr);
	}

	/* now free the main structure */
	delete_link(&psched->sc_link);
	(void) free(psched);
}

/**
 * @brief - purge scheduler from system
 *
 * @param[in]	psched	- The pointer to the delete to delete
 *
 * @return	error code
 * @retval	0	- scheduler purged
 * @retval	PBSE_OBJBUSY	- scheduler deletion not allowed
 */
int
sched_delete(pbs_sched *psched)
{
	pbs_db_obj_info_t obj;
	pbs_db_sched_info_t dbsched;
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;

	if (psched == NULL)
		return (0);

	/* TODO check for scheduler activity and return PBSE_OBJBUSY */
	/* delete scheduler from database */
	strcpy(dbsched.sched_name, psched->sc_name);
	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;
	if (pbs_db_delete_obj(conn, &obj) != 0) {
		snprintf(log_buffer, LOG_BUF_SIZE,
				"delete of scheduler %s from datastore failed",
				psched->sc_name);
		log_err(errno, __func__, log_buffer);
	}
	sched_free(psched);

	return (0);
}

/**
 * @brief
 * 		action routine for the sched's "sched_port" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_sched_port(attribute *pattr, void *pobj, int actmode)
{
	if (actmode == ATR_ACTION_ALTER) {
		/*TODO*/
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		action routine for the sched's "sched_priv" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_sched_priv(attribute *pattr, void *pobj, int actmode)
{
	pbs_sched* psched;
	if (actmode == ATR_ACTION_NEW || actmode == ATR_ACTION_ALTER) {
		psched = (pbs_sched*) GET_NEXT(svr_allscheds);
		while (psched != (pbs_sched*) 0) {
			if (psched->sch_attr[SCHED_ATR_sched_priv].at_flags & ATR_VFLAG_SET) {
				if (!strcmp(psched->sch_attr[SCHED_ATR_sched_priv].at_val.at_str, pattr->at_val.at_str)) {
					if (psched != pobj) {
						return PBSE_SCHED_PRIV_EXIST;
					} else
						break;
				}
			}
			psched = (pbs_sched*) GET_NEXT(psched->sc_link);
		}
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		action routine for the sched's "sched_log" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_sched_log(attribute *pattr, void *pobj, int actmode)
{
	pbs_sched* psched;
	if (actmode == ATR_ACTION_NEW || actmode == ATR_ACTION_ALTER) {
		psched = (pbs_sched*) GET_NEXT(svr_allscheds);
		while (psched != (pbs_sched*) 0) {
			if (psched->sch_attr[SCHED_ATR_sched_log].at_flags & ATR_VFLAG_SET) {
				if (!strcmp(psched->sch_attr[SCHED_ATR_sched_log].at_val.at_str, pattr->at_val.at_str)) {
					if (psched != pobj) {
						return PBSE_SCHED_LOG_EXIST;
					} else
						break;
				}
			}
			psched = (pbs_sched*) GET_NEXT(psched->sc_link);
		}
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		action routine for the sched's "sched_iteration" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_sched_iteration(attribute *pattr, void *pobj, int actmode)
{
	if (actmode == ATR_ACTION_ALTER) {
		/*TODO*/
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		action routine for the sched's "sched_user" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_sched_user(attribute *pattr, void *pobj, int actmode)
{
	if (actmode == ATR_ACTION_ALTER) {
		/*TODO*/
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		poke_scheduler - action routine for the server's "scheduling" attribute.
 *		Call the scheduler whenever the attribute is set (or reset) to true.
 *
 * @param[in]	pattr	-	pointer to attribute structure
 * @param[in]	pobj	-	not used
 * @param[in]	actmode	-	action mode
 *
 * @return	int
 * @retval	zero	: success
 */

int
poke_scheduler(attribute *pattr, void *pobj, int actmode)
{
	if (pobj == &server || pobj == dflt_scheduler) {
		if (actmode == ATR_ACTION_ALTER) {
			if (pattr->at_val.at_long)
				set_scheduler_flag(SCH_SCHEDULE_CMD);
		}
		/*TODO also set this attribute on main scheduler */
	} else {
		/*TODO need add the code for other scheduler apart from main scheduler. */
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		Sets default scheduler attributes
 *
 * @param[in]	psched	-	Scheduler
 */
void
set_sched_default(pbs_sched* psched)
{
	if (!psched)
		return;
	if ((psched->sch_attr[(int) SCHED_ATR_sched_cycle_len].at_flags & ATR_VFLAG_SET) == 0) {
		psched->sch_attr[(int) SCHED_ATR_sched_cycle_len].at_val.at_long = PBS_SCHED_CYCLE_LEN_DEFAULT;
		psched->sch_attr[(int) SCHED_ATR_sched_cycle_len].at_flags =
				ATR_VFLAG_DEFLT | ATR_VFLAG_SET | ATR_VFLAG_MODCACHE;
	}
	if ((psched->sch_attr[(int) SCHED_ATR_scheduling].at_flags & ATR_VFLAG_SET) == 0) {
		psched->sch_attr[(int) SCHED_ATR_scheduling].at_val.at_long = 1;
		psched->sch_attr[(int) SCHED_ATR_sched_cycle_len].at_flags =
				ATR_VFLAG_DEFLT | ATR_VFLAG_SET | ATR_VFLAG_MODCACHE;
	}
	if ((psched->sch_attr[(int) SCHED_ATR_sched_state].at_flags & ATR_VFLAG_SET) == 0) {
		psched->sch_attr[(int) SCHED_ATR_sched_state].at_val.at_str = malloc(SC_STATUS_LEN + 1);
		if (psched->sch_attr[(int) SCHED_ATR_sched_state].at_val.at_str == NULL)
			return;
		else {
			if (psched != dflt_scheduler)
				strncpy(psched->sch_attr[(int) SCHED_ATR_sched_state].at_val.at_str, SC_DOWN, SC_STATUS_LEN);
			else
				strncpy(psched->sch_attr[(int) SCHED_ATR_sched_state].at_val.at_str, SC_IDLE, SC_STATUS_LEN);
			psched->sch_attr[(int) SCHED_ATR_sched_state].at_val.at_str[SC_STATUS_LEN] = '\0';
			psched->sch_attr[(int) SCHED_ATR_sched_state].at_flags =
			ATR_VFLAG_DEFLT | ATR_VFLAG_SET | ATR_VFLAG_MODCACHE;
		}

	}
}


/**
 * @brief
 * 		action routine for the scheduler's partition attribute
 *
 * @param[in]	pattr	-	pointer to attribute structure
 * @param[in]	pobj	-	not used
 * @param[in]	actmode	-	action mode
 *
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */

int
action_sched_partition(attribute *pattr, void *pobj, int actmode)
{
	pbs_sched* psched;
	pbs_sched* pin_sched;
	attribute *part_attr;
	int i;
	int k;
	if (pobj == dflt_scheduler)
		return PBSE_OP_NOT_PERMITTED;
	pin_sched = (pbs_sched*) pobj;
	if (pin_sched->sch_attr[SCHED_ATR_partition].at_flags & ATR_VFLAG_SET)
		i = pin_sched->sch_attr[SCHED_ATR_partition].at_val.at_arst->as_usedptr;
	else
		i = 0;

	for (; i < pattr->at_val.at_arst->as_usedptr; ++i) {
		if (pattr->at_val.at_arst->as_string[i] == NULL)
			continue;
		psched = (pbs_sched*) GET_NEXT(svr_allscheds);
		while (psched) {
			part_attr = &(psched->sch_attr[SCHED_ATR_partition]);
			if (part_attr->at_flags & ATR_VFLAG_SET) {
				for (k = 0; k < part_attr->at_val.at_arst->as_usedptr; k++) {
					if ((part_attr->at_val.at_arst->as_string[k] != NULL)
							&& (!strcmp(pattr->at_val.at_arst->as_string[i],
									part_attr->at_val.at_arst->as_string[k])))
						return PBSE_PART_ALREADY_USED;
				}
			}
			psched = (pbs_sched*) GET_NEXT(psched->sc_link);
		}
	}
	return PBSE_NONE;
}
