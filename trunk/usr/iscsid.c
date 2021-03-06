/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "iscsid.h"

static u32 ttt;

static u32 get_next_ttt(struct connection *conn __attribute__((unused)))
{
	ttt += 1;
	return (ttt == ISCSI_RESERVED_TAG) ? ++ttt : ttt;
}

static struct iscsi_key login_keys[] = {
	{"InitiatorName",},
	{"InitiatorAlias",},
	{"SessionType",},
	{"TargetName",},
	{NULL, 0, 0, 0, NULL},
};

char *text_key_find(struct connection *conn, char *searchKey)
{
	char *data, *key, *value;
	int keylen, datasize;

	keylen = strlen(searchKey);
	data = conn->req.data;
	datasize = conn->req.datasize;

	while (1) {
		for (key = data; datasize > 0 && *data != '='; data++, datasize--)
			;
		if (!datasize)
			return NULL;
		data++;
		datasize--;

		for (value = data; datasize > 0 && *data != 0; data++, datasize--)
			;
		if (!datasize)
			return NULL;
		data++;
		datasize--;

		if (keylen == value - key - 1
		     && !strncmp(key, searchKey, keylen))
			return value;
	}
}

static char *next_key(char **data, int *datasize, char **value)
{
	char *key, *p, *q;
	int size = *datasize;

	key = p = *data;
	for (; size > 0 && *p != '='; p++, size--)
		;
	if (!size)
		return NULL;
	*p++ = 0;
	size--;

	for (q = p; size > 0 && *p != 0; p++, size--)
		;
	if (!size)
		return NULL;
	p++;
	size--;

	*data = p;
	*value = q;
	*datasize = size;

	return key;
}

static struct buf_segment * conn_alloc_buf_segment(struct connection *conn,
						   size_t sz)
{
	struct buf_segment *seg = malloc(sizeof *seg + sz);

	if (seg) {
		seg->len = 0;
		memset(seg->data, 0x0, sz);
		list_add_tail(&seg->entry, &conn->rsp_buf_list);
		log_debug(2, "alloc'ed new buf_segment");
	}

	return seg;
}


void text_key_add(struct connection *conn, char *key, char *value)
{
	struct buf_segment *seg;
	int keylen = strlen(key);
	int valuelen = strlen(value);
	int len = keylen + valuelen + 2;
	int off = 0;
	int sz = 0;
	int stage = 0;
	size_t data_sz;

	data_sz = (conn->state == STATE_FULL) ?
		conn->session_param[key_max_xmit_data_length].val :
		INCOMING_BUFSIZE;

	seg = list_empty(&conn->rsp_buf_list) ? NULL :
		list_entry(conn->rsp_buf_list.q_back, struct buf_segment,
			   entry);

	while (len) {
		if (!seg || seg->len == data_sz) {
			seg = conn_alloc_buf_segment(conn, data_sz);
			if (!seg) {
				log_error("Failed to alloc text buf segment\n");
				conn_free_rsp_buf_list(conn);
				break;
			}
		}
		switch (stage) {
		case 0:
			sz = min_t(int, data_sz - seg->len, keylen - off);
			strncpy(seg->data + seg->len, key + off, sz);
			if (sz == data_sz - seg->len) {
				off += sz;
				if (keylen - off == 0) {
					off = 0;
					stage++;
				}
			} else {
				off = 0;
				stage++;
			}
			break;
		case 1:
			seg->data[seg->len] = '=';
			off = 0;
			sz = 1;
			stage++;
			break;
		case 2:
			sz = min_t(int, data_sz - seg->len, valuelen - off);
			strncpy(seg->data + seg->len, value + off, sz);
			off += sz;
			if (valuelen - off == 0) {
				off = 0;
				stage++;
			}
			break;
		case 3:
			seg->data[seg->len] = 0;
			sz = 1;
			break;
		}

		log_debug(1, "wrote: %s", seg->data + seg->len);

		seg->len += sz;
		len -= sz;
	}
}

static void text_key_add_reject(struct connection *conn, char *key)
{
	text_key_add(conn, key, "Reject");
}

static int account_empty(u32 tid, int dir)
{
	char pass[ISCSI_NAME_LEN];

	memset(pass, 0, sizeof(pass));
	return cops->account_query(tid, dir, pass, pass) < 0 ? 1 : 0;
}

static void text_scan_security(struct connection *conn)
{
	struct iscsi_login_rsp_hdr *rsp = (struct iscsi_login_rsp_hdr *)&conn->rsp.bhs;
	char *key, *value, *data, *nextValue;
	int datasize;

	data = conn->req.data;
	datasize = conn->req.datasize;

	while ((key = next_key(&data, &datasize, &value))) {
		if (!(param_index_by_name(key, login_keys) < 0))
			;
		else if (!strcmp(key, "AuthMethod")) {
			do {
				nextValue = strchr(value, ',');
				if (nextValue)
					*nextValue++ = 0;

				if (!strcmp(value, "None")) {
					if (!account_empty(conn->tid, AUTH_DIR_INCOMING))
						continue;
					conn->auth_method = AUTH_NONE;
					text_key_add(conn, key, "None");
					break;
				} else if (!strcmp(value, "CHAP")) {
					if (account_empty(conn->tid, AUTH_DIR_INCOMING))
						continue;
					conn->auth_method = AUTH_CHAP;
					text_key_add(conn, key, "CHAP");
					break;
				}
			} while ((value = nextValue));

			if (conn->auth_method == AUTH_UNKNOWN)
				text_key_add_reject(conn, key);
		} else
			text_key_add(conn, key, "NotUnderstood");
	}
	if (conn->auth_method == AUTH_UNKNOWN) {
		rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
		rsp->status_detail = ISCSI_STATUS_AUTH_FAILED;
		conn->state = STATE_EXIT;
	}
}

static void login_security_done(struct connection *conn)
{
	int err;
	struct iscsi_login_req_hdr *req = (struct iscsi_login_req_hdr *)&conn->req.bhs;
	struct iscsi_login_rsp_hdr *rsp = (struct iscsi_login_rsp_hdr *)&conn->rsp.bhs;
	struct session *session;

	if (!conn->tid)
		return;

	if ((session = session_find_name(conn->tid, conn->initiator, req->sid))) {
		if (!req->sid.id.tsih) {
			/* do session reinstatement */
			session_conns_close(conn->tid, session->sid.id64);
			session = NULL;
		} else if (req->sid.id.tsih != session->sid.id.tsih) {
			/* fail the login */
			rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
			rsp->status_detail = ISCSI_STATUS_SESSION_NOT_FOUND;
			conn->state = STATE_EXIT;
			return;
		} else if ((err = conn_test(conn)) == -ENOENT) {
			/* do connection reinstatement */
		}
		/* add a new connection to the session */
		conn->session = session;
	} else {
		if (req->sid.id.tsih) {
			/* fail the login */
			rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
			rsp->status_detail = ISCSI_STATUS_SESSION_NOT_FOUND;
			conn->state = STATE_EXIT;
			return;
		}
		/* instantiate a new session */
	}
}

static void text_scan_login(struct connection *conn)
{
	char *key, *value, *data;
	int datasize, idx;
	struct iscsi_login_rsp_hdr *rsp = (struct iscsi_login_rsp_hdr *)&conn->rsp.bhs;

	data = conn->req.data;
	datasize = conn->req.datasize;

	while ((key = next_key(&data, &datasize, &value))) {
		if (!(param_index_by_name(key, login_keys) < 0))
			;
		else if (!strcmp(key, "AuthMethod"))
			;
		else if (!((idx = param_index_by_name(key, session_keys)) < 0)) {
			int err;
			unsigned int val;
			char buf[32];

			if (idx == key_max_xmit_data_length) {
				text_key_add(conn, key, "NotUnderstood");
				continue;
			}
			if (idx == key_max_recv_data_length)
				idx = key_max_xmit_data_length;

			if (param_str_to_val(session_keys, idx, value, &val) < 0) {
				if (conn->session_param[idx].state
				    == KEY_STATE_START) {
					text_key_add_reject(conn, key);
					continue;
				} else {
					rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
					rsp->status_detail = ISCSI_STATUS_INIT_ERR;
					conn->state = STATE_EXIT;
					goto out;
				}
			}

			err = param_check_val(session_keys, idx, &val);
			err = param_set_val(session_keys, conn->session_param, idx, &val);

			switch (conn->session_param[idx].state) {
			case KEY_STATE_START:
				if (idx == key_max_xmit_data_length)
					break;
				memset(buf, 0, sizeof(buf));
				param_val_to_str(session_keys, idx, val, buf);
				text_key_add(conn, key, buf);
				break;
			case KEY_STATE_REQUEST:
				if (val != conn->session_param[idx].val) {
					rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
					rsp->status_detail = ISCSI_STATUS_INIT_ERR;
					conn->state = STATE_EXIT;
					log_warning("%s %u %u\n", key,
					val, conn->session_param[idx].val);
					goto out;
				}
				break;
			case KEY_STATE_DONE:
				break;
			}
			conn->session_param[idx].state = KEY_STATE_DONE;
		} else
			text_key_add(conn, key, "NotUnderstood");
	}

out:
	return;
}

static int text_check_param(struct connection *conn)
{
	struct iscsi_param *p = conn->session_param;
	char buf[32];
	int i, cnt;

	for (i = 0, cnt = 0; session_keys[i].name; i++) {
		if (p[i].state == KEY_STATE_START && p[i].val != session_keys[i].def) {
			switch (conn->state) {
			case STATE_LOGIN_FULL:
			case STATE_SECURITY_FULL:
				if (i == key_max_xmit_data_length) {
					if (p[i].val > session_keys[i].def)
						p[i].val = session_keys[i].def;
					p[i].state = KEY_STATE_DONE;
					continue;
				}
				break;
			case STATE_LOGIN:
				if (i == key_max_xmit_data_length)
					continue;
				memset(buf, 0, sizeof(buf));
				param_val_to_str(session_keys, i, p[i].val,
						 buf);
				text_key_add(conn, session_keys[i].name, buf);
				if (i == key_max_recv_data_length) {
					p[i].state = KEY_STATE_DONE;
					continue;
				}
				p[i].state = KEY_STATE_REQUEST;
				break;
			default:
				if (i == key_max_xmit_data_length)
					continue;
			}
			cnt++;
		}
	}

	return cnt;
}

static void login_rsp_ini_err(struct connection *conn, int status_detail)
{
	struct iscsi_login_rsp_hdr * const rsp =
		(struct iscsi_login_rsp_hdr * const)&conn->rsp.bhs;

	rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
	rsp->status_detail = status_detail;
	conn->state = STATE_EXIT;
}

static void login_start(struct connection *conn)
{
	struct iscsi_login_req_hdr *req = (struct iscsi_login_req_hdr *)&conn->req.bhs;

	char *name, *alias, *session_type, *target_name;
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(struct sockaddr_storage);

	memset(&ss, 0, sizeof(ss));

	conn->cid = be16_to_cpu(req->cid);
	conn->sid.id64 = req->sid.id64;
	if (!conn->sid.id64) {
		login_rsp_ini_err(conn, ISCSI_STATUS_MISSING_FIELDS);
		return;
	}

	name = text_key_find(conn, "InitiatorName");
	if (!name) {
		login_rsp_ini_err(conn, ISCSI_STATUS_MISSING_FIELDS);
		return;
	}
	conn->initiator = strdup(name);
	alias = text_key_find(conn, "InitiatorAlias");
	session_type = text_key_find(conn, "SessionType");
	target_name = text_key_find(conn, "TargetName");

	conn->auth_method = -1;
	conn->session_type = SESSION_NORMAL;

	getsockname(conn->fd, (struct sockaddr *) &ss, &slen);

	if (session_type) {
		if (!strcmp(session_type, "Discovery"))
			conn->session_type = SESSION_DISCOVERY;
		else if (strcmp(session_type, "Normal")) {
			login_rsp_ini_err(conn, ISCSI_STATUS_INV_SESSION_TYPE);
			return;
		}
	}

	if (conn->session_type == SESSION_NORMAL) {
		if (!target_name) {
			login_rsp_ini_err(conn, ISCSI_STATUS_MISSING_FIELDS);
			return;
		}

		struct target * const target =
			target_find_by_name(target_name);

		if (!target
		    || !cops->initiator_allow(target->tid, conn->fd, name)
		    || !cops->target_allow(target->tid, (struct sockaddr *) &ss)
		    || !isns_scn_allow(target->tid, name)) {
			login_rsp_ini_err(conn, ISCSI_STATUS_TGT_NOT_FOUND);
			return;
		}

		conn->tid = target->tid;

		if (target->max_nr_sessions &&
		    (++target->nr_sessions > target->max_nr_sessions)) {
			--target->nr_sessions;
			log_debug(1, "rejecting session for target '%s': "
				  "too many sessions", target_name);
			login_rsp_ini_err(conn, ISCSI_STATUS_TOO_MANY_CONN);
			return;
		}

		if (ki->param_get(conn->tid, 0, key_session,
				  conn->session_param))
			login_rsp_ini_err(conn, ISCSI_STATUS_SVC_UNAVAILABLE);
	}
	conn->exp_cmd_sn = be32_to_cpu(req->cmd_sn);
	log_debug(1, "exp_cmd_sn: %d,%d", conn->exp_cmd_sn, req->cmd_sn);
	text_key_add(conn, "TargetPortalGroupTag", "1");
}

static void login_finish(struct connection *conn)
{
	switch (conn->session_type) {
	case SESSION_NORMAL:
		if (!conn->session)
			session_create(conn);
		conn->sid = conn->session->sid;
		break;
	case SESSION_DISCOVERY:
		/* set a dummy tsih value */
		conn->sid.id.tsih = 1;
		break;
	}
}

static void cmnd_reject(struct connection *conn, u8 reason)
{
	struct iscsi_reject_hdr *rej =
		(struct iscsi_reject_hdr *)&conn->rsp.bhs;
	size_t data_sz = sizeof(struct iscsi_hdr);
	struct buf_segment *seg;

	conn_free_rsp_buf_list(conn);
	seg = conn_alloc_buf_segment(conn, data_sz);

	memset(rej, 0x0, sizeof *rej);
	rej->opcode = ISCSI_OP_REJECT_MSG;
	rej->reason = reason;
	rej->ffffffff = ISCSI_RESERVED_TAG;
	rej->flags |= ISCSI_FLG_FINAL;

	rej->stat_sn = cpu_to_be32(conn->stat_sn++);
	rej->exp_cmd_sn = cpu_to_be32(conn->exp_cmd_sn);
	conn->max_cmd_sn = conn->exp_cmd_sn + 1;
	rej->max_cmd_sn = cpu_to_be32(conn->max_cmd_sn);

	if (!seg) {
		log_error("Failed to alloc data segment for Reject PDU\n");
		return;
	}

	memcpy(seg->data, &conn->req.bhs, data_sz);
	seg->len = data_sz;
}

static int cmnd_exec_auth(struct connection *conn)
{
       int res;

        switch (conn->auth_method) {
        case AUTH_CHAP:
                res = cmnd_exec_auth_chap(conn);
                break;
        case AUTH_NONE:
                res = 0;
                break;
        default:
                log_error("Unknown auth. method %d", conn->auth_method);
                res = -3;
        }

        return res;
}

static void cmnd_exec_login(struct connection *conn)
{
	struct iscsi_login_req_hdr *req = (struct iscsi_login_req_hdr *)&conn->req.bhs;
	struct iscsi_login_rsp_hdr *rsp = (struct iscsi_login_rsp_hdr *)&conn->rsp.bhs;
	int stay = 0, nsg_disagree = 0;

	memset(rsp, 0, BHS_SIZE);
	if ((req->opcode & ISCSI_OPCODE_MASK) != ISCSI_OP_LOGIN_CMD ||
	    !(req->opcode & ISCSI_OP_IMMEDIATE)) {
		cmnd_reject(conn, ISCSI_REASON_PROTOCOL_ERROR);
		return;
	}

	rsp->opcode = ISCSI_OP_LOGIN_RSP;
	rsp->max_version = ISCSI_VERSION;
	rsp->active_version = ISCSI_VERSION;
	rsp->itt = req->itt;

	if (/*req->max_version < ISCSI_VERSION ||*/
	    req->min_version > ISCSI_VERSION) {
		rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
		rsp->status_detail = ISCSI_STATUS_NO_VERSION;
		conn->state = STATE_EXIT;
		return;
	}

	switch (req->flags & ISCSI_FLG_CSG_MASK) {
	case ISCSI_FLG_CSG_SECURITY:
		log_debug(1, "Login request (security negotiation): %d", conn->state);
		rsp->flags = ISCSI_FLG_CSG_SECURITY;

		switch (conn->state) {
		case STATE_FREE:
			conn->state = STATE_SECURITY;
			login_start(conn);
			if (rsp->status_class)
				return;
			//else fall through
		case STATE_SECURITY:
			text_scan_security(conn);
			if (rsp->status_class)
				return;
			if (conn->auth_method != AUTH_NONE) {
				conn->state = STATE_SECURITY_AUTH;
				conn->auth_state = AUTH_STATE_START;
			}
			break;
		case STATE_SECURITY_AUTH:
			switch (cmnd_exec_auth(conn)) {
			case 0:
				break;
			default:
			case -1:
				goto init_err;
			case -2:
				goto auth_err;
			}
			break;
		default:
			goto init_err;
		}

		break;
	case ISCSI_FLG_CSG_LOGIN:
		log_debug(1, "Login request (operational negotiation): %d", conn->state);
		rsp->flags = ISCSI_FLG_CSG_LOGIN;

		switch (conn->state) {
		case STATE_FREE:
			conn->state = STATE_LOGIN;

			login_start(conn);
			if (!account_empty(conn->tid, AUTH_DIR_INCOMING))
				goto auth_err;
			if (rsp->status_class)
				return;
			text_scan_login(conn);
			if (rsp->status_class)
				return;
			stay = text_check_param(conn);
			break;
		case STATE_LOGIN:
			text_scan_login(conn);
			if (rsp->status_class)
				return;
			stay = text_check_param(conn);
			break;
		default:
			goto init_err;
		}
		break;
	default:
		goto init_err;
	}

	if (rsp->status_class)
		return;
	if (conn->state != STATE_SECURITY_AUTH && req->flags & ISCSI_FLG_TRANSIT) {
		int nsg = req->flags & ISCSI_FLG_NSG_MASK;

		switch (nsg) {
		case ISCSI_FLG_NSG_LOGIN:
			switch (conn->state) {
			case STATE_SECURITY:
			case STATE_SECURITY_DONE:
				conn->state = STATE_SECURITY_LOGIN;
				login_security_done(conn);
				break;
			default:
				goto init_err;
			}
			break;
		case ISCSI_FLG_NSG_FULL_FEATURE:
			switch (conn->state) {
			case STATE_SECURITY:
			case STATE_SECURITY_DONE:
				if ((nsg_disagree = text_check_param(conn))) {
					conn->state = STATE_LOGIN;
					nsg = ISCSI_FLG_NSG_LOGIN;
					break;
				}
				conn->state = STATE_SECURITY_FULL;
				login_security_done(conn);
				break;
			case STATE_LOGIN:
				if (stay)
					nsg = ISCSI_FLG_NSG_LOGIN;
				else
					conn->state = STATE_LOGIN_FULL;
				break;
			default:
				goto init_err;
			}
			if (!stay && !nsg_disagree) {
				text_check_param(conn);
				login_finish(conn);
			}
			break;
		default:
			goto init_err;
		}
		rsp->flags |= nsg | (stay ? 0 : ISCSI_FLG_TRANSIT);
	}

	/*
	 * TODO: support Logical Text Data Segments > INCOMING_BUFSIZE (i.e.
	 * key=value pairs spanning several PDUs) during login phase
	 */
	if (!list_empty(&conn->rsp_buf_list) &&
	    !list_length_is_one(&conn->rsp_buf_list)) {
		log_error("Target error: \'key=value\' pairs spanning several "
			  "Login PDUs are not implemented, yet\n");
		goto target_err;
	}

	rsp->sid = conn->sid;
	rsp->stat_sn = cpu_to_be32(conn->stat_sn++);
	rsp->exp_cmd_sn = cpu_to_be32(conn->exp_cmd_sn);
	conn->max_cmd_sn = conn->exp_cmd_sn + 1;
	rsp->max_cmd_sn = cpu_to_be32(conn->max_cmd_sn);
	return;
init_err:
	rsp->flags = 0;
	rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
	rsp->status_detail = ISCSI_STATUS_INIT_ERR;
	conn->state = STATE_EXIT;
	return;
auth_err:
	rsp->flags = 0;
	rsp->status_class = ISCSI_STATUS_INITIATOR_ERR;
	rsp->status_detail = ISCSI_STATUS_AUTH_FAILED;
	conn->state = STATE_EXIT;
	return;
 target_err:
	rsp->flags = 0;
	rsp->status_class = ISCSI_STATUS_TARGET_ERR;
	rsp->status_detail = ISCSI_STATUS_TARGET_ERROR;
	conn->state = STATE_EXIT;
	return;
}

static void text_scan_text(struct connection *conn)
{
	char *key, *value, *data;
	int datasize;

	data = conn->req.data;
	datasize = conn->req.datasize;

	while ((key = next_key(&data, &datasize, &value))) {
		if (!strcmp(key, "SendTargets")) {
			if (value[0] == 0)
				continue;

			target_list_build(conn,
					  strcmp(value, "All") ? value : NULL);
		} else
			text_key_add(conn, key, "NotUnderstood");
	}
}

static void cmnd_exec_text(struct connection *conn)
{
	struct iscsi_text_req_hdr *req = (struct iscsi_text_req_hdr *)&conn->req.bhs;
	struct iscsi_text_rsp_hdr *rsp = (struct iscsi_text_rsp_hdr *)&conn->rsp.bhs;

	memset(rsp, 0, BHS_SIZE);

	rsp->opcode = ISCSI_OP_TEXT_RSP;
	rsp->itt = req->itt;
	conn->exp_cmd_sn = be32_to_cpu(req->cmd_sn);
	if (!(req->opcode & ISCSI_OP_IMMEDIATE))
		conn->exp_cmd_sn++;

	log_debug(1, "Text request: %d", conn->state);

	if (req->ttt == ISCSI_RESERVED_TAG) {
		conn_free_rsp_buf_list(conn);
		text_scan_text(conn);
		if (!list_empty(&conn->rsp_buf_list) &&
		    !list_length_is_one(&conn->rsp_buf_list))
			conn->ttt = get_next_ttt(conn);
		else
			conn->ttt = ISCSI_RESERVED_TAG;
	} else if (list_empty(&conn->rsp_buf_list) || conn->ttt != req->ttt) {
		log_error("Rejecting unexpected text request. TTT recv %#x, "
			  "expected %#x; %stext segments queued\n",
			  req->ttt, conn->ttt, list_empty(&conn->rsp_buf_list) ?
			  "no " : "");
		cmnd_reject(conn, ISCSI_REASON_INVALID_PDU_FIELD);
		return;
	}

	if (list_empty(&conn->rsp_buf_list) ||
	    list_length_is_one(&conn->rsp_buf_list)) {
		rsp->flags = ISCSI_FLG_FINAL;
		conn->ttt = ISCSI_RESERVED_TAG;
	}

	rsp->ttt = conn->ttt;

	rsp->stat_sn = cpu_to_be32(conn->stat_sn++);
	rsp->exp_cmd_sn = cpu_to_be32(conn->exp_cmd_sn);
	conn->max_cmd_sn = conn->exp_cmd_sn + 1;
	rsp->max_cmd_sn = cpu_to_be32(conn->max_cmd_sn);
}

static void cmnd_exec_logout(struct connection *conn)
{
	struct iscsi_logout_req_hdr *req = (struct iscsi_logout_req_hdr *)&conn->req.bhs;
	struct iscsi_logout_rsp_hdr *rsp = (struct iscsi_logout_rsp_hdr *)&conn->rsp.bhs;

	memset(rsp, 0, BHS_SIZE);
	rsp->opcode = ISCSI_OP_LOGOUT_RSP;
	rsp->flags = ISCSI_FLG_FINAL;
	rsp->itt = req->itt;
	conn->exp_cmd_sn = be32_to_cpu(req->cmd_sn);
	if (!(req->opcode & ISCSI_OP_IMMEDIATE))
		conn->exp_cmd_sn++;

	rsp->stat_sn = cpu_to_be32(conn->stat_sn++);
	rsp->exp_cmd_sn = cpu_to_be32(conn->exp_cmd_sn);
	conn->max_cmd_sn = conn->exp_cmd_sn + 1;
	rsp->max_cmd_sn = cpu_to_be32(conn->max_cmd_sn);
}

int cmnd_execute(struct connection *conn)
{
	struct buf_segment *seg;
	struct iscsi_login_rsp_hdr *login_rsp;

	switch (conn->req.bhs.opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN_CMD:
		if (conn->state == STATE_FULL) {
			cmnd_reject(conn, ISCSI_REASON_PROTOCOL_ERROR);
			break;
		}
		cmnd_exec_login(conn);
		login_rsp = (struct iscsi_login_rsp_hdr *) &conn->rsp.bhs;
		if (login_rsp->status_class)
			conn_free_rsp_buf_list(conn);
		break;
	case ISCSI_OP_TEXT_CMD:
		if (conn->state != STATE_FULL)
			cmnd_reject(conn, ISCSI_REASON_PROTOCOL_ERROR);
		else
			cmnd_exec_text(conn);
		break;
	case ISCSI_OP_LOGOUT_CMD:
		if (conn->state != STATE_FULL)
			cmnd_reject(conn, ISCSI_REASON_PROTOCOL_ERROR);
		else
			cmnd_exec_logout(conn);
		break;
	default:
		cmnd_reject(conn, ISCSI_REASON_UNSUPPORTED_COMMAND);
		return 0;
	}

	if (!list_empty(&conn->rsp_buf_list)) {
		seg = list_entry(conn->rsp_buf_list.q_forw,
				 struct buf_segment, entry);
		list_del_init(&seg->entry);
		conn->rsp.datasize = seg->len;
		conn->rsp.data = seg->data;
	} else {
		conn->rsp.datasize = 0;
		conn->rsp.data = NULL;
	}

	conn->rsp.bhs.ahslength = conn->rsp.ahssize / 4;
	conn->rsp.bhs.datalength[0] = conn->rsp.datasize >> 16;
	conn->rsp.bhs.datalength[1] = conn->rsp.datasize >> 8;
	conn->rsp.bhs.datalength[2] = conn->rsp.datasize;
	log_pdu(2, &conn->rsp);

	return 1;
}

void cmnd_finish(struct connection *conn)
{
	struct buf_segment *seg;

	if (conn->rsp.data) {
		seg = container_of(conn->rsp.data, struct buf_segment, data);
		list_del(&seg->entry);
		free(seg);
		conn->rsp.data = NULL;
	}

	switch (conn->state) {
	case STATE_EXIT:
		conn->state = STATE_CLOSE;
		break;
	case STATE_SECURITY_LOGIN:
		conn->state = STATE_LOGIN;
		break;
	case STATE_SECURITY_FULL:
		//fall through
	case STATE_LOGIN_FULL:
		if (conn->session_type == SESSION_NORMAL)
			conn->state = STATE_KERNEL;
		else
			conn->state = STATE_FULL;
		break;
	}
}
