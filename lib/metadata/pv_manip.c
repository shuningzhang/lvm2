/*
 * Copyright (C) 2003 Sistina Software, Inc. All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU Lesser General Public License v.2.1.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "lib.h"
#include "metadata.h"
#include "pv_alloc.h"
#include "toolcontext.h"
#include "archiver.h"
#include "locking.h"
#include "lvmcache.h"

static struct pv_segment *_alloc_pv_segment(struct dm_pool *mem,
					    struct physical_volume *pv,
					    uint32_t pe, uint32_t len,
					    struct lv_segment *lvseg,
					    uint32_t lv_area)
{
	struct pv_segment *peg;

	if (!(peg = dm_pool_zalloc(mem, sizeof(*peg)))) {
		log_error("pv_segment allocation failed");
		return NULL;
	}

	peg->pv = pv;
	peg->pe = pe;
	peg->len = len;
	peg->lvseg = lvseg;
	peg->lv_area = lv_area;

	list_init(&peg->list);

	return peg;
}

int alloc_pv_segment_whole_pv(struct dm_pool *mem, struct physical_volume *pv)
{
	struct pv_segment *peg;

	if (!pv->pe_count)
		return 1;

	/* FIXME Cope with holes in PVs */
	if (!(peg = _alloc_pv_segment(mem, pv, 0, pv->pe_count, NULL, 0))) {
		stack;
		return 0;
	}

	list_add(&pv->segments, &peg->list);

	return 1;
}

int peg_dup(struct dm_pool *mem, struct list *peg_new, struct list *peg_old)
{
	struct pv_segment *peg, *pego;

	list_init(peg_new);

	list_iterate_items(pego, peg_old) {
		if (!(peg = _alloc_pv_segment(mem, pego->pv, pego->pe,
					      pego->len, pego->lvseg,
					      pego->lv_area))) {
			stack;
			return 0;
		} 
		list_add(peg_new, &peg->list);
	}

	return 1;
}

/*
 * Split peg at given extent.
 * Second part is always deallocated.
 */
static int _pv_split_segment(struct physical_volume *pv, struct pv_segment *peg,
			     uint32_t pe)
{
	struct pv_segment *peg_new;

	if (!(peg_new = _alloc_pv_segment(pv->fmt->cmd->mem, peg->pv, pe,
					  peg->len + peg->pe - pe,
					  NULL, 0))) {
		stack;
		return 0;
	}

	peg->len = peg->len - peg_new->len;

	list_add_h(&peg->list, &peg_new->list);

	if (peg->lvseg) {
		peg->pv->pe_alloc_count -= peg_new->len;
		peg->lvseg->lv->vg->free_count += peg_new->len;
	}

	return 1;
}

/*
 * Ensure there is a PV segment boundary at the given extent.
 */
int pv_split_segment(struct physical_volume *pv, uint32_t pe)
{
	struct pv_segment *peg;

	if (pe == pv->pe_count)
		return 1;

	if (!(peg = find_peg_by_pe(pv, pe))) {
		log_error("Segment with extent %" PRIu32 " in PV %s not found",
			  pe, dev_name(pv->dev));
		return 0;
	}

	/* This is a peg start already */
	if (pe == peg->pe)
		return 1;

	if (!_pv_split_segment(pv, peg, pe)) {
		stack;
		return 0;
	}

	return 1;
}

static struct pv_segment null_pv_segment = {
	.pv = NULL,
	.pe = 0,
};

struct pv_segment *assign_peg_to_lvseg(struct physical_volume *pv,
				       uint32_t pe, uint32_t area_len,
				       struct lv_segment *seg,
				       uint32_t area_num)
{
	struct pv_segment *peg;

	/* Missing format1 PV */
	if (!pv)
		return &null_pv_segment;

	if (!pv_split_segment(pv, pe) || 
	    !pv_split_segment(pv, pe + area_len)) {
		stack;
		return NULL;
	}

	if (!(peg = find_peg_by_pe(pv, pe))) {
		log_error("Missing PV segment on %s at %u.",
			  dev_name(pv->dev), pe);
		return NULL;
	}

	peg->lvseg = seg;
	peg->lv_area = area_num;

	peg->pv->pe_alloc_count += area_len;
	peg->lvseg->lv->vg->free_count -= area_len;

	return peg;
}

int release_pv_segment(struct pv_segment *peg, uint32_t area_reduction)
{
	if (!peg->lvseg) {
		log_error("release_pv_segment with unallocated segment: "
			  "%s PE %" PRIu32, dev_name(peg->pv->dev), peg->pe);
		return 0;
	}

	if (peg->lvseg->area_len == area_reduction) {
		peg->pv->pe_alloc_count -= area_reduction;
		peg->lvseg->lv->vg->free_count += area_reduction;

		peg->lvseg = NULL;
		peg->lv_area = 0;

		/* FIXME merge free space */

		return 1;
	}

	if (!pv_split_segment(peg->pv, peg->pe + peg->lvseg->area_len -
				       area_reduction)) {
		stack;
		return 0;
	}

	return 1;
}

/*
 * Only for use by lv_segment merging routines.
 */
void merge_pv_segments(struct pv_segment *peg1, struct pv_segment *peg2)
{
	peg1->len += peg2->len;

	list_del(&peg2->list);
}

/*
 * Check all pv_segments in VG for consistency
 */
int check_pv_segments(struct volume_group *vg)
{
	struct physical_volume *pv;
	struct pv_list *pvl;
	struct pv_segment *peg;
	unsigned s, segno;
	uint32_t start_pe, alloced;
	uint32_t pv_count = 0, free_count = 0, extent_count = 0;
	int ret = 1;

	list_iterate_items(pvl, &vg->pvs) {
		pv = pvl->pv;
		segno = 0;
		start_pe = 0;
		alloced = 0;
		pv_count++;

		list_iterate_items(peg, &pv->segments) {
			s = peg->lv_area;

			/* FIXME Remove this next line eventually */
			log_debug("%s %u: %6u %6u: %s(%u:%u)",
				  dev_name(pv->dev), segno++, peg->pe, peg->len,
				  peg->lvseg ? peg->lvseg->lv->name : "NULL",
				  peg->lvseg ? peg->lvseg->le : 0, s);
			/* FIXME Add details here on failure instead */
			if (start_pe != peg->pe) {
				log_error("Gap in pvsegs: %u, %u",
					  start_pe, peg->pe);
				ret = 0;
			}
			if (peg->lvseg) {
				if (seg_type(peg->lvseg, s) != AREA_PV) {
					log_error("Wrong lvseg area type");
					ret = 0;
				}
				if (seg_pvseg(peg->lvseg, s) != peg) {
					log_error("Inconsistent pvseg pointers");
					ret = 0;
				}
				if (peg->lvseg->area_len != peg->len) {
					log_error("Inconsistent length: %u %u",
						  peg->len,
						  peg->lvseg->area_len);
					ret = 0;
				}
				alloced += peg->len;
			}
			start_pe += peg->len;
		}

		if (start_pe != pv->pe_count) {
			log_error("PV segment pe_count mismatch: %u != %u",
				  start_pe, pv->pe_count);
			ret = 0;
		}

		if (alloced != pv->pe_alloc_count) {
			log_error("PV segment pe_alloc_count mismatch: "
				  "%u != %u", alloced, pv->pe_alloc_count);
			ret = 0;
		}

		extent_count += start_pe;
		free_count += (start_pe - alloced);
	}

	if (pv_count != vg->pv_count) {
		log_error("PV segment VG pv_count mismatch: %u != %u",
			  pv_count, vg->pv_count);
		ret = 0;
	}

	if (free_count != vg->free_count) {
		log_error("PV segment VG free_count mismatch: %u != %u",
			  free_count, vg->free_count);
		ret = 0;
	}

	if (extent_count != vg->extent_count) {
		log_error("PV segment VG extent_count mismatch: %u != %u",
			  extent_count, vg->extent_count);
		ret = 0;
	}

	return ret;
}

static int _reduce_pv(struct physical_volume *pv, struct volume_group *vg, uint32_t new_pe_count)
{
	struct pv_segment *peg, *pegt;
	uint32_t old_pe_count = pv->pe_count;

	if (new_pe_count < pv->pe_alloc_count) {
		log_error("%s: cannot resize to %" PRIu32 " extents "
			  "as %" PRIu32 " are allocated.",
			  dev_name(pv->dev), new_pe_count,
			  pv->pe_alloc_count);
		return 0;
	}

	/* Check PEs to be removed are not already allocated */
	list_iterate_items(peg, &pv->segments) {
 		if (peg->pe + peg->len <= new_pe_count)
			continue;

		if (peg->lvseg) {
			log_error("%s: cannot resize to %" PRIu32 " extents as "
				  "later ones are allocated.",
				  dev_name(pv->dev), new_pe_count);
			return 0;
		}
	}

	if (!pv_split_segment(pv, new_pe_count)) {
		stack;
		return 0;
	}

	list_iterate_items_safe(peg, pegt, &pv->segments) {
 		if (peg->pe + peg->len > new_pe_count)
			list_del(&peg->list);
	}

	pv->pe_count = new_pe_count;

	vg->extent_count -= (old_pe_count - new_pe_count);
	vg->free_count -= (old_pe_count - new_pe_count);

	return 1;
}

static int _extend_pv(struct physical_volume *pv, struct volume_group *vg,
		      uint32_t new_pe_count)
{
	struct pv_segment *peg;
	uint32_t old_pe_count = pv->pe_count;

	if ((uint64_t) new_pe_count * pv->pe_size > pv->size ) {
		log_error("%s: cannot resize to %" PRIu32 " extents as there "
			  "is only room for %" PRIu64 ".", dev_name(pv->dev),
			  new_pe_count, pv->size / pv->pe_size);
		return 0;
	}

	peg = _alloc_pv_segment(pv->fmt->cmd->mem, pv,
				old_pe_count,
				new_pe_count - old_pe_count,
				NULL, 0);
	list_add(&pv->segments, &peg->list);

	pv->pe_count = new_pe_count;

	vg->extent_count += (new_pe_count - old_pe_count);
	vg->free_count += (new_pe_count - old_pe_count);

	return 1;
}

/*
 * Resize a PV in a VG, adding or removing segments as needed.
 * New size must fit within pv->size.
 */
int pv_resize(struct physical_volume *pv,
	      struct volume_group *vg,
	      uint32_t new_pe_count)
{
	if ((new_pe_count == pv->pe_count)) {
		log_verbose("No change to size of physical volume %s.",
			    dev_name(pv->dev));
		return 1;
	}

	log_verbose("Resizing physical volume %s from %" PRIu32
		    " to %" PRIu32 " extents.",
		    dev_name(pv->dev), pv->pe_count, new_pe_count);

	if (new_pe_count > pv->pe_count)
		return _extend_pv(pv, vg, new_pe_count);
	else
		return _reduce_pv(pv, vg, new_pe_count);
}

int pv_resize_single(struct cmd_context *cmd,
		     struct volume_group *vg,
		     struct physical_volume *pv,
		     uint64_t new_size)
{
	struct pv_list *pvl;
	int consistent = 1;
	uint64_t size = 0;
	uint32_t new_pe_count = 0;
	struct list mdas;
	const char *pv_name = dev_name(pv_dev(pv));
	const char *vg_name;

	list_init(&mdas);

	if (!*pv_vg_name(pv)) {
		vg_name = ORPHAN;

		if (!lock_vol(cmd, vg_name, LCK_VG_WRITE)) {
			log_error("Can't get lock for orphans");
			return 0;
		}

		if (!(pv = pv_read(cmd, pv_name, &mdas, NULL, 1))) {
			unlock_vg(cmd, vg_name);
			log_error("Unable to read PV \"%s\"", pv_name);
			return 0;
		}

		/* FIXME Create function to test compatibility properly */
		if (list_size(&mdas) > 1) {
			log_error("%s: too many metadata areas for pvresize",
				  pv_name);
			unlock_vg(cmd, vg_name);
			return 0;
		}
	} else {
		vg_name = pv_vg_name(pv);

		if (!lock_vol(cmd, vg_name, LCK_VG_WRITE)) {
			log_error("Can't get lock for %s", pv_vg_name(pv));
			return 0;
		}

		if (!(vg = vg_read(cmd, vg_name, NULL, &consistent))) {
			unlock_vg(cmd, vg_name);
			log_error("Unable to find volume group of \"%s\"",
				  pv_name);
			return 0;
		}

		if (!vg_check_status(vg, CLUSTERED | EXPORTED_VG | LVM_WRITE)) {
			unlock_vg(cmd, vg_name);
			return 0;
		}

		if (!(pvl = find_pv_in_vg(vg, pv_name))) {
			unlock_vg(cmd, vg_name);
			log_error("Unable to find \"%s\" in volume group \"%s\"",
				  pv_name, vg->name);
			return 0;
		}

		pv = pvl->pv;

		if (!archive(vg))
			return 0;
	}

	if (!(pv->fmt->features & FMT_RESIZE_PV)) {
		log_error("Physical volume %s format does not support resizing.",
			  pv_name);
		unlock_vg(cmd, vg_name);
		return 0;
	}

	/* Get new size */
	if (!dev_get_size(pv_dev(pv), &size)) {
		log_error("%s: Couldn't get size.", pv_name);
		unlock_vg(cmd, vg_name);
		return 0;
	}
	
	if (new_size) {
		if (new_size > size)
			log_warn("WARNING: %s: Overriding real size. "
				  "You could lose data.", pv_name);
		log_verbose("%s: Pretending size is %" PRIu64 " not %" PRIu64
			    " sectors.", pv_name, new_size, pv_size(pv));
		size = new_size;
	}

	if (size < PV_MIN_SIZE) {
		log_error("%s: Size must exceed minimum of %ld sectors.",
			  pv_name, PV_MIN_SIZE);
		unlock_vg(cmd, vg_name);
		return 0;
	}

	if (size < pv_pe_start(pv)) {
		log_error("%s: Size must exceed physical extent start of "
			  "%" PRIu64 " sectors.", pv_name, pv_pe_start(pv));
		unlock_vg(cmd, vg_name);
		return 0;
	}

	pv->size = size;

	if (vg) {
		pv->size -= pv_pe_start(pv);
		new_pe_count = pv_size(pv) / vg->extent_size;
		
 		if (!new_pe_count) {
			log_error("%s: Size must leave space for at "
				  "least one physical extent of "
				  "%" PRIu32 " sectors.", pv_name,
				  pv_pe_size(pv));
			unlock_vg(cmd, vg_name);
			return 0;
		}

		if (!pv_resize(pv, vg, new_pe_count)) {
			stack;
			unlock_vg(cmd, vg_name);
			return 0;
		}
	}

	log_verbose("Resizing volume \"%s\" to %" PRIu64 " sectors.",
		    pv_name, pv_size(pv));

	log_verbose("Updating physical volume \"%s\"", pv_name);
	if (*pv_vg_name(pv)) {
		if (!vg_write(vg) || !vg_commit(vg)) {
			unlock_vg(cmd, pv_vg_name(pv));
			log_error("Failed to store physical volume \"%s\" in "
				  "volume group \"%s\"", pv_name, vg->name);
			return 0;
		}
		backup(vg);
		unlock_vg(cmd, vg_name);
	} else {
		if (!(pv_write(cmd, pv, NULL, INT64_C(-1)))) {
			unlock_vg(cmd, ORPHAN);
			log_error("Failed to store physical volume \"%s\"",
				  pv_name);
			return 0;
		}
		unlock_vg(cmd, vg_name);
	}

	log_print("Physical volume \"%s\" changed", pv_name);
	return 1;
}
