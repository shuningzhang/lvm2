/*
 * dmfs-error.c
 *
 * Copyright (C) 2001 Sistina Software
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/fs.h>

#include "dm.h"

void dmfs_add_error(struct dm_table *t, unsigned num, char *str)
{

}

/* 
 * Assuming that we allow modules to call this rather than passing the message
 * back. Not sure which approach to take yet.
 */
EXPORT_SYMBOL(dmfs_add_error);

static ssize_t dmfs_error_read(struct file *file, char *buf, size_t size, loff_t *pos)
{
	struct dmfs_i *dmi = DMFS_I(file->f_dentry->d_parent->d_inode);
	struct dm_table *t = dmi->table;
	int copied = 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	down(&dmi->sem);
	if (dmi->table) {

	}
	up(&dmi->sem);
	return copied;
}

static int dmfs_error_sync(struct file *file, struct dentry *dentry, int datasync)
{
	return 0;
}

static struct dm_table_file_operations = {
	read:		dmfs_error_read,
	fsync:		dmfs_error_sync,
};

static struct dmfs_error_inode_operations = {
};

int dmfs_create_error(struct inode *dir, int mode)
{
	struct inode *inode = new_inode(dir->i_sb);

	if (inode) {
		inode->i_mode = mode | S_IFREG;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		inode->i_fop = &dmfs_error_file_operations;
		inode->i_op = &dmfs_error_inode_operations;
	}

	return inode;
}

