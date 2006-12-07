/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"

static struct iscsi_initiator *initiator_lookup(struct iscsi_target *target,
						char *name)
{
	struct iscsi_initiator *initiator;

	list_for_each_entry(initiator, &target->initiator_list, list) {
		if (!strcmp(name, initiator->name))
			return initiator;
	}
	return NULL;
}

static struct iscsi_initiator *initiator_alloc(struct iscsi_target *target,
					       struct session_info *info)
{
	struct iscsi_initiator *initiator;
	struct iscsi_session *session;

	dprintk(D_SETUP, "%p %u %#Lx\n", target, target->tid,
		(unsigned long long) info->sid);

	initiator = kzalloc(sizeof(*initiator), GFP_KERNEL);
	if (!initiator)
		return NULL;

	initiator->usage_count = 1;
	initiator->iid = ++target->initiator_iid_count;

	initiator->name = kstrdup(info->initiator_name, GFP_KERNEL);
	if (!initiator->name) {
		kfree(initiator);
		return NULL;
	}

	dprintk(D_SETUP, "%s %u\n", initiator->name, initiator->iid);
	list_add(&initiator->list, &target->initiator_list);

	session = session_lookup(target, info->sid);
	BUG_ON(!session);
	session->rinitiator = initiator;

	return initiator;
}

static void initiator_free(struct iscsi_initiator *initiator)
{
	list_del(&initiator->list);
	kfree(initiator->name);
	kfree(initiator);
}

static int initiator_add(struct iscsi_target *target, struct session_info *info)
{
	struct iscsi_initiator *initiator;
	struct iscsi_session *session;

	dprintk(D_SETUP, "%s\n", info->initiator_name);

	initiator = initiator_lookup(target, info->initiator_name);
	if (initiator)
		initiator->usage_count++;
	else {
		initiator = initiator_alloc(target, info);
		if (!initiator)
			return -ENOMEM;
	}

	session = session_lookup(target, info->sid);
	BUG_ON(!session);
	session->rinitiator = initiator;
	return 0;
}

static void initiator_del(struct iscsi_target *target, char *name)
{
	struct iscsi_initiator *initiator;

	initiator = initiator_lookup(target, name);
	if (initiator && !--initiator->usage_count)
		initiator_free(initiator);
}

struct iscsi_session *session_lookup(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		if (session->sid == sid)
			return session;
	}
	return NULL;
}

static struct iscsi_session *
iet_session_alloc(struct iscsi_target *target, struct session_info *info)
{
	int i;
	struct iscsi_session *session;

	dprintk(D_SETUP, "%p %u %#Lx\n", target, target->tid,
		(unsigned long long) info->sid);

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return NULL;

	session->target = target;
	session->sid = info->sid;
	memcpy(&session->param, &target->sess_param, sizeof(session->param));
	session->max_queued_cmnds = target->trgt_param.queued_cmnds;

	session->exp_cmd_sn = info->exp_cmd_sn;
	session->max_cmd_sn = info->max_cmd_sn;

	session->initiator = kstrdup(info->initiator_name, GFP_KERNEL);
	if (!session->initiator) {
		kfree(session);
		return NULL;
	}

	INIT_LIST_HEAD(&session->conn_list);
	INIT_LIST_HEAD(&session->pending_list);

	spin_lock_init(&session->cmnd_hash_lock);
	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++)
		INIT_LIST_HEAD(&session->cmnd_hash[i]);

	session->next_ttt = 1;

	list_add(&session->list, &target->session_list);

	return session;
}

static int session_free(struct iscsi_session *session)
{
	int i;

	dprintk(D_SETUP, "%#Lx\n", (unsigned long long) session->sid);

	assert(list_empty(&session->conn_list));

	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++) {
		if (!list_empty(&session->cmnd_hash[i]))
			BUG();
	}

	list_del(&session->list);

	kfree(session->initiator);
	kfree(session);

	return 0;
}

int session_add(struct iscsi_target *target, struct session_info *info)
{
	struct iscsi_session *session;
	int err = -EEXIST;

	if ((session = session_lookup(target, info->sid)))
		return err;

	session = iet_session_alloc(target, info);
	if (session) {
		err = initiator_add(target, info);
		if (err)
			session_free(session);
	} else
		err = -ENOMEM;

	return err;
}

int session_del(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	if (!(session = session_lookup(target, sid)))
		return -ENOENT;

	if (!list_empty(&session->conn_list)) {
		eprintk("%llu still have connections\n", (unsigned long long) session->sid);
		return -EBUSY;
	}

	initiator_del(target, session->initiator);

	return session_free(session);
}

static void iet_session_info_show(struct seq_file *seq, struct iscsi_target *target)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		seq_printf(seq, "\tsid:%llu initiator:%s\n",
			   (unsigned long long) session->sid, session->initiator);
		conn_info_show(seq, session);
	}
}

static int iet_sessions_info_show(struct seq_file *seq, void *v)
{
	return iet_info_show(seq, iet_session_info_show);
}

static int iet_session_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, iet_sessions_info_show, NULL);
}

struct file_operations session_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= iet_session_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
