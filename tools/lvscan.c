/*
 * Copyright (C) 2001  Sistina Software
 *
 * LVM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * LVM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LVM; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "tools.h"

static int lvscan_single(struct cmd_context *cmd, struct logical_volume *lv);

int lvscan(struct cmd_context *cmd, int argc, char **argv)
{
	if (argc) {
		log_error("No additional command line arguments allowed");
		return EINVALID_CMD_LINE;
	}

	return process_each_lv(cmd, argc, argv, LCK_VG_READ, &lvscan_single);

/*********** FIXME Count!   Add private struct to process_each*  
*	if (!lv_total)
*		log_print("no logical volumes found");
*	else {
*		log_print
*		    ("%d logical volumes with %s total in %d volume group%s",
*		     lv_total, (dummy =
*				display_size(lv_capacity_total / 2, SIZE_SHORT)),
*		     vg_total, vg_total == 1 ? "" : "s");
*		dbg_free(dummy);
*		dummy = NULL;
*		if (lv_active > 0)
*			printf("%d active", lv_active);
*		if (lv_active > 0 && lv_total - lv_active > 0)
*			printf(" / ");
*		if (lv_total - lv_active > 0)
*			printf("%d inactive", lv_total - lv_active);
*		printf(" logical volumes\n");
*	}
*************/

}

static int lvscan_single(struct cmd_context *cmd, struct logical_volume *lv)
{
	struct dm_info info;
	int lv_total = 0;
	ulong lv_capacity_total = 0;

	char *dummy;
	const char *active_str, *snapshot_str;

/* FIXME Add -D arg to skip this! */
	if (lv_info(lv, &info) && info.exists)
		active_str = "ACTIVE   ";
	else
		active_str = "inactive ";

	if (lv_is_origin(lv))
		snapshot_str = "Original";
	else if (lv_is_cow(lv))
		snapshot_str = "Snapshot";
	else
		snapshot_str = "        ";

/********** FIXME Snapshot
		if (lv->status & SNAPSHOT)
			dummy =
			    display_size(lv->lv_remap_end *
					 lv->lv_chunk_size / 2,
					 SIZE_SHORT);
		else
***********/
	dummy = display_size(lv->size / 2, SIZE_SHORT);

	log_print("%s%s '%s%s/%s' [%s]%s%s", active_str, snapshot_str,
		  cmd->dev_dir, lv->vg->name, lv->name, dummy,
		  (lv->status & ALLOC_STRICT) ? " strict" : "",
		  (lv->status & ALLOC_CONTIGUOUS) ? " contiguous" : "");

	dbg_free(dummy);

	/* FIXME sprintf? */

/*********** FIXME Handle segments?
	if (lv->segments[0]->stripes > 1 && !(lv->status & SNAPSHOT))
		log_print(" striped[%u]", lv->segments[0]->stripes);
****************/

/******** FIXME Device number display & Snapshot
		if (arg_count(cmd,blockdevice_ARG))
			printf(" %d:%d",
			       MAJOR(lv->lv_dev),
			       MINOR(lv->lv_dev));
		else 
			if (lv->status & SNAPSHOT)
			printf(" of %s", lv->lv_snapshot_org->name);
*****************/

	lv_total++;

/******** FIXME Snapshot
		if (lv->status & SNAPSHOT)
			lv_capacity_total +=
			    lv->lv_remap_end * lv->lv_chunk_size
		else
********/
	lv_capacity_total += lv->size;

	return 0;
}
