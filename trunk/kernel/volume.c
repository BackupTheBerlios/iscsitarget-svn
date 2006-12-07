/*
 * Volume manager
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

#include <linux/parser.h>

struct iet_volume *volume_lookup(struct iscsi_target *target, u32 lun)
{
	struct iet_volume *volume;

	list_for_each_entry(volume, &target->volumes, list) {
		if (volume->lun == lun)
			return volume;
	}
	return NULL;
}

enum {
	Opt_type,
	Opt_iomode,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_type, "Type=%s"},
	{Opt_iomode, "IOMode=%s"},
	{Opt_err, NULL},
};

static int set_iotype(struct iet_volume *volume, char *params)
{
	int err = 0;
	substring_t args[MAX_OPT_ARGS];
	char *p, *argp = NULL, *buf = (char *) get_zeroed_page(GFP_USER);

	if (!buf)
		return -ENOMEM;
	strncpy(buf, params, PAGE_CACHE_SIZE);

	while ((p = strsep(&buf, ",")) != NULL) {
		int token;

		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_type:
			if (!(argp = match_strdup(&args[0])))
				err = -ENOMEM;
			if (argp && !(volume->iotype = get_iotype(argp)))
				err = -ENOENT;
			kfree(argp);
			break;
		case Opt_iomode:
			if (!(argp = match_strdup(&args[0])))
				err = -ENOMEM;
			if (argp && !strcmp(argp, "ro"))
				SetLUReadonly(volume);
			else if (argp && !strcmp(argp, "wb"))
				SetLUAsync(volume);
			kfree(argp);
			break;
		default:
			break;
		}
	}

	if (!err && !volume->iotype && !(volume->iotype = get_iotype("fileio"))) {
		eprintk("%s\n", "Cannot find fileio");
		err = -EINVAL;
	}

	free_page((unsigned long) buf);

	return err;
}

int volume_add(struct iscsi_target *target, struct volume_info *info)
{
	int err = 0;
	struct iet_volume *volume;

	if ((volume = volume_lookup(target, info->lun)))
		return -EEXIST;

	if (info->lun > 0x3fff)
		return -EINVAL;

	if (!(volume = kmalloc(sizeof(*volume), GFP_KERNEL)))
		return -ENOMEM;
	memset(volume, 0, sizeof(*volume));

	volume->target = target;
	volume->lun = info->lun;

	if ((err = set_iotype(volume, info->args)) < 0)
		goto err;

	if ((err = volume->iotype->attach(volume, info->args)) < 0)
		goto err;

	INIT_LIST_HEAD(&volume->queue.wait_list);
	spin_lock_init(&volume->queue.queue_lock);
	spin_lock_init(&volume->reserve_lock);

	volume->l_state = IDEV_RUNNING;
	atomic_set(&volume->l_count, 0);

	list_add_tail(&volume->list, &target->volumes);
	atomic_inc(&target->nr_volumes);

err:
	if (err && volume) {
		put_iotype(volume->iotype);
		kfree(volume);
	}

	return err;
}

void iscsi_volume_destroy(struct iet_volume *volume)
{
	assert(volume->l_state == IDEV_DEL);
	assert(!atomic_read(&volume->l_count));

	volume->iotype->detach(volume);
	put_iotype(volume->iotype);
	list_del(&volume->list);
	kfree(volume);
}

int iscsi_volume_del(struct iscsi_target *target, struct volume_info *info)
{
	struct iet_volume *volume;

	eprintk("%x %x\n", target->tid, info->lun);
	if (!(volume = volume_lookup(target, info->lun)))
		return -ENOENT;

	volume->l_state = IDEV_DEL;
	atomic_dec(&target->nr_volumes);
	if (!atomic_read(&volume->l_count))
		iscsi_volume_destroy(volume);

	return 0;
}

struct iet_volume *volume_get(struct iscsi_target *target, u32 lun)
{
	struct iet_volume *volume;

	if ((volume = volume_lookup(target, lun))) {
		if (volume->l_state == IDEV_RUNNING)
			atomic_inc(&volume->l_count);
		else
			volume = NULL;
	}
	return volume;
}

void volume_put(struct iet_volume *volume)
{
	if (atomic_dec_and_test(&volume->l_count) && volume->l_state == IDEV_DEL)
		iscsi_volume_destroy(volume);
}

int volume_reserve(struct iet_volume *volume, u32 iid)
{
	if (!volume)
		return -ENOENT;

	spin_lock(&volume->reserve_lock);
	if (volume->reserve_iid && volume->reserve_iid != iid) {
		spin_unlock(&volume->reserve_lock);
		return -EBUSY;
	}

	volume->reserve_iid = iid;
	spin_unlock(&volume->reserve_lock);

	return 0;
}

int is_volume_reserved(struct iet_volume *volume, u32 iid)
{
	if (!volume || !volume->reserve_iid || volume->reserve_iid == iid)
		return 0;

	return -EBUSY;
}

int volume_release(struct iet_volume *volume, u32 iid, int force)
{
	if (force || volume->reserve_iid == iid)
		volume->reserve_iid = 0;

	return 0;
}

static void iet_volume_info_show(struct seq_file *seq, struct iscsi_target *target)
{
	struct iet_volume *volume;

	list_for_each_entry(volume, &target->volumes, list) {
		seq_printf(seq, "\tlun:%u state:%x iotype:%s",
			   volume->lun, volume->l_state, volume->iotype->name);
		if (LUReadonly(volume))
			seq_printf(seq, " iomode:ro");
		else if (LUAsync(volume))
			seq_printf(seq, " iomode:wb");
		else
			seq_printf(seq, " iomode:wt");
		if (volume->iotype->show)
			volume->iotype->show(volume, seq);
		else
			seq_printf(seq, "\n");
	}
}

static int iet_volumes_info_show(struct seq_file *seq, void *v)
{
	return iet_info_show(seq, iet_volume_info_show);
}

static int iet_volume_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, iet_volumes_info_show, NULL);
}

struct file_operations volume_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= iet_volume_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
