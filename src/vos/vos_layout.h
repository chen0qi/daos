/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * Layout definition for VOS root object
 * vos/vos_layout.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef _VOS_LAYOUT_H
#define _VOS_LAYOUT_H
#include <libpmemobj.h>
#include <daos/btree.h>
#include <daos_srv/evtree.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/eio.h>
#include <daos_srv/vea.h>

/**
 * VOS metadata structure declarations
 * TODO: use df (durable format) as postfix of structures.
 *
 * opaque structure expanded inside implementation
 * Container table for holding container UUIDs
 * Object table for holding object IDs
 * B-Tree for Key Value stores
 * EV-Tree for Byte array stores
 */
struct vos_cont_table_df;
struct vos_cont_df;
struct vos_obj_table_df;
struct vos_obj_df;
struct vos_cookie_rec_df;
struct vos_epoch_index;
struct vos_krec_df;
struct vos_irec_df;

/**
 * Typed Layout named using Macros from libpmemobj
 * Each structure is assigned a type number internally
 * in the macro libpmemobj has its own pointer type (PMEMoid)
 * and uses named unions to assign types to such pointers.
 * Type-number help identify the PMEMoid  pointer types
 * with an ID. This layout structure is used to consicely
 * represent the types associated with the vos_pool_layout.
 * In this case consecutive typeIDs are assigned to the
 * different pointers in the pool.
 */
POBJ_LAYOUT_BEGIN(vos_pool_layout);

POBJ_LAYOUT_ROOT(vos_pool_layout, struct vos_pool_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cont_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cont_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cookie_rec_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_krec_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_irec_df);
POBJ_LAYOUT_END(vos_pool_layout);


/**
 * VOS container table
 * PMEM container index in each pool
 */
struct vos_cont_table_df {
	struct btr_root		ctb_btree;
};

/**
 * tree record stored in cookie index table
 * NB: it is actually stored in DRAM for now, but might switch to PMEM in
 * the future.
 */
struct vos_cookie_rec_df {
	/** max updated epoch of a cookie(upper level open handle) */
	daos_epoch_t		cr_max_epoch;
};

/**
 * VOS Pool root object
 */
struct vos_pool_df {
	/* Structs stored in LE or BE representation */
	uint32_t				pd_magic;
	/* Unique PoolID for each VOS pool assigned on creation */
	uuid_t					pd_id;
	/* Flags for compatibility features */
	uint64_t				pd_compat_flags;
	/* Flags for incompatibility features */
	uint64_t				pd_incompat_flags;
	/* Typed PMEMoid pointer for the container index table */
	struct vos_cont_table_df		pd_ctab_df;
	/* Pool info of objects, containers, space availability */
	vos_pool_info_t				pd_pool_info;
	/* Free space tracking for NVMe device */
	struct vea_space_df			pd_vea_df;
};

struct vos_epoch_index {
	struct btr_root		ehtable;
};

/**
 * VOS object table
 * It is just a in-place btree for the time being.
 */
struct vos_obj_table_df {
	struct btr_root			obt_btr;
};

/* VOS Container Value */
struct vos_cont_df {
	uuid_t				cd_id;
	vos_cont_info_t			cd_info;
	struct vos_obj_table_df		cd_otab_df;
	/*
	 * Allocation hint for block allocator, it can be turned into
	 * a hint vector when we need to support multiple active epochs.
	 */
	struct vea_hint_df		cd_hint_df;
};

/** btree (d/a-key) record bit flags */
enum vos_krec_bf {
	KREC_BF_EVT			= (1 << 0),
};

/**
 * Persisted VOS (d)key record, it is referenced by btr_record::rec_mmid
 * of btree VOS_BTR_KEY.
 */
struct vos_krec_df {
	/** record bitmap, e.g. has evtree, see vos_krec_bf */
	uint8_t				kr_bmap;
	/** checksum type */
	uint8_t				kr_cs_type;
	/** key checksum size (in bytes) */
	uint8_t				kr_cs_size;
	/** padding bytes */
	uint8_t				kr_pad_8;
	/** key length */
	uint32_t			kr_size;
	/** punched epoch, it's infinity if it's never been punched */
	uint64_t			kr_punched;
	/** btree root under the key */
	struct btr_root			kr_btr;
	/** evtree root, which is only used by akey */
	struct evt_root			kr_evt[0];
	/* Checksum and key are stored after tree root */
};

/**
 * Persisted VOS index & epoch record, it is referenced by btr_record::rec_mmid
 * of btree VOS_BTR_IDX.
 */
struct vos_irec_df {
	/** reserved for resolving overwrite race */
	uint64_t			ir_cookie;
	/** key checksum type */
	uint8_t				ir_cs_type;
	/** key checksum size (in bytes) */
	uint8_t				ir_cs_size;
	/** padding bytes */
	uint16_t			ir_pad16;
	/** pool map version */
	uint32_t			ir_ver;
	/** length of value */
	uint64_t			ir_size;
	/** external payload address */
	eio_addr_t			ir_ex_addr;
	/** placeholder for the key checksum & internal value */
	char				ir_body[0];
};

/**
 * VOS object, assume all objects are KV store...
 * NB: PMEM data structure.
 */
struct vos_obj_df {
	daos_unit_oid_t			vo_id;
	/** Attributes of object.  See vos_oi_attr */
	uint64_t			vo_oi_attr;
	/** Epoch when this object was punched, it's infinity by defaut. */
	uint64_t			vo_punched;
	/**
	 * Incarnation of the object, it's increased each time it's punched.
	 */
	uint64_t			vo_incarnation;
	/** VOS object btree root */
	struct btr_root			vo_tree;
};
#endif
