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
 * @file    svr_recov_db.c
 *
 * @brief
 * 		svr_recov_db.c - contains functions to save server state and recover
 *
 * Included functions are:
 *	svr_recov_db()
 *	svr_save_db()
 *	update_svrlive()
 *	svr_to_db_svr()
 *	db_to_svr_svr()
 *	svr_to_db_sched()
 *	db_to_svr_sched()
 *	sched_recov_db()
 *	sched_save_db()
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "pbs_ifl.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "server.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "log.h"
#include "pbs_db.h"
#include "pbs_sched.h"
#include "pbs_share.h"

/* Global Data Items: */

extern struct server server;
extern pbs_list_head svr_queues;
extern attribute_def svr_attr_def[];
extern char	*path_priv;
extern time_t	time_now;
extern char	*msg_svdbopen;
extern char	*msg_svdbnosv;
extern char	*path_svrlive;

#ifndef PBS_MOM
extern char *pbs_server_name;
extern char *pbs_server_id;
extern pbs_db_conn_t	*svr_db_conn;
#endif

#ifdef NAS /* localmod 005 */
/* External Functions Called */
extern int save_attr_db(pbs_db_conn_t *conn, pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef, struct attribute *pattr,
	int numattr, int newparent);
extern int recov_attr_db(pbs_db_conn_t *conn,
	void *parent,
	pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef,
	struct attribute *pattr,
	int limit,
	int unknown);
extern int utimes(const char *path, const struct timeval *times);
#endif /* localmod 005 */
extern pbs_sched *sched_alloc(char *sched_name);

/**
 * @brief
 *		Update the $PBS_HOME/server_priv/svrlive file timestamp
 *
 * @return	Error code
 * @retval	0	: Success
 * @retval	-1	: Failed to update timestamp
 *
 */
int
update_svrlive()
{
	static int fdlive = -1;
	if (fdlive == -1) {
		/* first time open the file */
		fdlive = open(path_svrlive, O_WRONLY | O_CREAT, 0600);
		if (fdlive < 0)
			return -1;
#ifdef WIN32
		secure_file(path_svrlive, "Administrators",
			READS_MASK|WRITES_MASK|STANDARD_RIGHTS_REQUIRED);
#endif
	}
	(void)utimes(path_svrlive, NULL);
	return 0;
}

/**
 * @brief
 *		Load a database server object from a server object in the server
 *
 * @param[in]	ps	-	Address of the server in pbs server
 * @param[out]	pdbsvr	-	Address of the database server object
 *
 */
static void
svr_to_db_svr(struct server *ps, pbs_db_svr_info_t *pdbsvr)
{
	memset(pdbsvr, 0, sizeof(pbs_db_svr_info_t));
	strcpy(pdbsvr->sv_name, pbs_server_id);
	strcpy(pdbsvr->sv_hostname, pbs_server_name);
	pdbsvr->sv_numjobs = ps->sv_qs.sv_numjobs;
	pdbsvr->sv_numque = ps->sv_qs.sv_numque;
	pdbsvr->sv_jobidnumber = ps->sv_qs.sv_jobidnumber;
}

/**
 * @brief
 *		Load a server object in pbs_server from a database server object
 *
 * @param[out]	ps	-	Address of the server in pbs server
 * @param[in]	pdbsvr	-	Address of the database server object
 *
 *
 */
void
db_to_svr_svr(struct server *ps, pbs_db_svr_info_t *pdbsvr)
{
	ps->sv_qs.sv_numjobs = pdbsvr->sv_numjobs;
	ps->sv_qs.sv_numque = pdbsvr->sv_numque;
	ps->sv_qs.sv_savetm = pdbsvr->sv_savetm;
	ps->sv_qs.sv_jobidnumber = pdbsvr->sv_jobidnumber;
}

/**
 * @brief
 *		Load a scheduler object in pbs_server from a database scheduler object
 *
 * @param[in]	ps	-	Address of the scheduler in pbs server
 * @param[in]	pdbsched	-	Address of the database scheduler object
 *
 */
static void
svr_to_db_sched(pbs_sched *ps, pbs_db_sched_info_t *pdbsched)
{
	strcpy(pdbsched->sched_name, ps->sc_name);
	strcpy(pdbsched->sched_sv_name, pbs_server_id);
}

/**
 * @brief
 *		Load a database scheduler object from the scheduler object in server
 *
 * @param[in]	ps	-	Address of the scheduler in pbs server
 * @param[in]	pdbsched	-	Address of the database scheduler object
 *
 */
static void
db_to_svr_sched(pbs_sched *ps, pbs_db_sched_info_t *pdbsched)
{
	strcpy(ps->sc_name,pdbsched->sched_name);
}

/**
 * @brief
 *		Recover server information and attributes from server database
 *
 * @par FunctionalitY:
 *		This function is only called on Server initialization at start up.
 *
 * @par	Note:
 *		server structure, extern struct server server, must be preallocated and
 *		all default values should already be set.
 *
 * @see	pbsd_init.c
 *
 * @return	Error code
 * @retval	0	: On successful recovery and creation of server structure
 * @retval	-1	: On failutre to open or read file.
 *
 */
int
svr_recov_db(void)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_svr_info_t dbsvr;
	pbs_db_attr_info_t attr_info;
	pbs_db_obj_info_t obj;

	/* load server_qs */
	strcpy(dbsvr.sv_name, pbs_server_id);

	if (pbs_db_begin_trx(conn, 0, 0) !=0)
		goto db_err;

	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	/* read in job fixed sub-structure */
	if (pbs_db_load_obj(conn, &obj) != 0)
		goto db_err;

	db_to_svr_svr(&server, &dbsvr);

	attr_info.parent_id = pbs_server_id;
	attr_info.parent_obj_type = PARENT_TYPE_SERVER; /* svr attr */

	/* read in server attributes */
	if (recov_attr_db(conn, &server, &attr_info, svr_attr_def, server.sv_attr,
		(int)SRV_ATR_LAST, 0) != 0)
		goto db_err;

	if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
		goto db_err;

	return (0);
db_err:
	log_err(-1, "svr_recov", "error on recovering server attr");
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	return -1;
}

/**
 * @brief
 *		Save the state of the server, server quick save sub structure and
 *		optionally the attributes.
 *
 * @par Functionality:
 *		Saving is done in one of two modes:
 *		Quick - only the "quick save sub structure" is saved
 *		Full  - The quick save sub structure is saved, the set
 *		and non-default valued attributes are then saved by calling
 *		save_attr_db()
 *
 * @param[in]	ps   -	Pointer to struct server
 * @param[in]	mode -  type of save, either SVR_SAVE_QUICK or SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0	: Successful save of data.
 * @retval	-1	: Failure
 *
 */

int
svr_save_db(struct server *ps, int mode)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_svr_info_t dbsvr;
	pbs_db_attr_info_t attr_info;
	pbs_db_obj_info_t obj;
	int flag=0;

	ps->sv_qs.sv_savetm = time_now;

	/* as part of the server save, update svrlive file now,
	 * used in failover
	 */
	if (update_svrlive() !=0)
		return -1;

	svr_to_db_svr(ps, &dbsvr);
	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	if (mode == SVR_SAVE_QUICK) {
		/* save server_qs */
		if (pbs_db_update_obj(conn, &obj) != 0)
			goto db_err;
	} else {	/* SVR_SAVE_FULL Save */
		if (pbs_db_begin_trx(conn, 0, 0) !=0)
			goto db_err;

		if (mode == SVR_SAVE_NEW) {
			if (pbs_db_insert_obj(conn, &obj) != 0)
				goto db_err;
			flag = 1;
		} else { /* FULL SAVE */
			/*
			 * remove all old attributes
			 * and insert them back again
			 */

			pg_db_delete_svrattr(conn, &obj);
			/* server_qs */
			if (pbs_db_update_obj(conn, &obj) != 0)
				goto db_err;
			flag = 1; /* so all set attributes are re-inserted back */
		}

		/* svr_attrs */
		attr_info.parent_obj_type = PARENT_TYPE_SERVER; /* svr attr */
		attr_info.parent_id = pbs_server_id;

		if (save_attr_db(conn, &attr_info, svr_attr_def, ps->sv_attr, (int)SRV_ATR_LAST, flag) !=0)
			goto db_err;

		if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
			goto db_err;
	}
	return (0);
db_err:
	strcpy(log_buffer, msg_svdbnosv);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	panic_stop_db(log_buffer);
	return (-1);
}




static char *schedemsg = "unable to save scheddb ";

/**
 * @brief Recover Schedulers
 *
 * @see	pbsd_init.c
 *
 *
 * @return	Error code
 * @retval	 0 :	On successful recovery and creation of server structure
 * @retval	-1 :	On failure to open or read file.
 * @retval	-2 :	No schedulers found.
 * */

int
sched_recov_db(void)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_sched_info_t dbsched;
	pbs_db_attr_info_t attr_info;
	pbs_db_obj_info_t obj;
	int rc;
	int index;
	int count;
	void *state = NULL;
	pbs_sched *psched;

	/* start a transaction */
	if (pbs_db_begin_trx(conn, 0, 0) != 0)
		return (-1);

	/* get scheds from DB */
	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;

	state = pbs_db_cursor_init(conn, &obj, NULL);
	if (state == NULL) {
		snprintf(log_buffer, LOG_BUF_SIZE,
				"%s", (char *) conn->conn_db_err);
		log_err(-1, __func__, log_buffer);
		pbs_db_cursor_close(conn, state);
		(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
		return -1;
	}
	count = pbs_db_get_rowcount(state);
	if (count <= 0) {
		/* no schedulers found in DB*/
		pbs_db_cursor_close(conn, state);
		(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
		return -2;
	}
	while ((rc = pbs_db_cursor_next(conn, state, &obj)) == 0) {
		/* recover sched */
		psched = sched_alloc(dbsched.sched_name);
		if(!strncmp(dbsched.sched_name, PBS_DFLT_SCHED_NAME,
						strlen(PBS_DFLT_SCHED_NAME))) {
			dflt_scheduler = psched;
		}
		db_to_svr_sched(psched, &dbsched);

		attr_info.parent_id = psched->sc_name;
		attr_info.parent_obj_type = PARENT_TYPE_SCHED; /* svr attr */

		/* read in attributes */
		if (recov_attr_db(conn, psched, &attr_info, sched_attr_def, psched->sch_attr,
			(int)SCHED_ATR_LAST, 0) != 0)
			goto db_err;

		if (pbs_conf.pbs_use_tcp == 0) {
			/* check if throughput mode is visible in non-TPP mode, if so make it invisible */
			psched->sch_attr[SCHED_ATR_throughput_mode].at_flags = 0;
		}
	}
	pbs_db_cursor_close(conn, state);
	if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
		goto db_err;

	if (server.sv_attr[SRV_ATR_scheduling].at_val.at_long)
		set_scheduler_flag(SCH_SCHEDULE_ETE_ON);

	return 0;

db_err:
	log_err(-1, "sched_recov", "read of scheduler db failed");
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	return -1;
}


/**
 * @brief
 *		Save the state of the scheduler structure which consists only of
 *		attributes.
 *
 * @par Functionality:
 *		Saving is done only in Full mode:
 *		Full  - The attributes which are set and non-default are saved by
 *		save_attr_db()
 *
 * @param[in]	ps   -	Pointer to struct sched
 * @param[in]	mode -  type of save, only SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0 :	Successful save of data.
 * @retval	-1 :	Failure
 *
 */

int
sched_save_db(pbs_sched *ps, int mode)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_sched_info_t dbsched;
	pbs_db_attr_info_t attr_info;
	pbs_db_obj_info_t obj;
	int flag=0;

	svr_to_db_sched(ps, &dbsched);
	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;

	if (mode == SVR_SAVE_QUICK) {
		/* save server_qs */
		if (pbs_db_update_obj(conn, &obj) != 0)
			goto db_err;
	} else {	/* SVR_SAVE_FULL Save */
		if (pbs_db_begin_trx(conn, 0, 0) !=0)
			goto db_err;

		if (mode == SVR_SAVE_NEW) {
			if (pbs_db_insert_obj(conn, &obj) != 0)
				goto db_err;
			flag = 1;
		} else {

			/* server_qs */
			if (pbs_db_update_obj(conn, &obj) != 0) {
				if (pbs_db_insert_obj(conn, &obj) != 0)
					goto db_err;
			}
		}

		/* svr_attrs */
		attr_info.parent_obj_type = PARENT_TYPE_SCHED; /* svr attr */
		attr_info.parent_id = ps->sc_name;

		if (save_attr_db(conn, &attr_info, sched_attr_def, ps->sch_attr, (int)SCHED_ATR_LAST, flag) !=0)
			goto db_err;

		if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
			goto db_err;
	}
	return (0);

db_err:
	strcpy(log_buffer, schedemsg);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	panic_stop_db(log_buffer);
	return (-1);
}
