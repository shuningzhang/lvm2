/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#ifndef _LVM_FORMAT1_H
#define _LVM_FORMAT1_H

#include "metadata.h"

#ifdef LVM1_INTERNAL
struct format_type *init_lvm1_format(struct cmd_context *cmd);
#endif

#endif
