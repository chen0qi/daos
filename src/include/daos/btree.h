/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * This file is part of daos
 *
 * src/include/daos/btree.h
 */

#ifndef __DAOS_BTREE_H__
#define __DAOS_BTREE_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos/mem.h>

enum {
	BTR_UMEM_TYPE	= 100,
	BTR_UMEM_ROOT	= (BTR_UMEM_TYPE + 0),
	BTR_UMEM_NODE	= (BTR_UMEM_TYPE + 1),
};

struct btr_root;
struct btr_node;

TMMID_DECLARE(struct btr_root, BTR_UMEM_ROOT);
TMMID_DECLARE(struct btr_node, BTR_UMEM_NODE);

/**
 * KV record of the btree.
 *
 * NB: could be PM data structure.
 */
struct btr_record {
	/**
	 * It could either be memory ID for the child node, or body of this
	 * record. The record body could be any of varous things:
	 *
	 * - the value address of KV record.
	 * - a structure includes both the variable-length key and value.
	 * - a complex data structure under this record, e.g. a sub-tree.
	 */
	umem_id_t		rec_mmid;
	/**
	 * Fix-size key can be stored in if it is small enough (DAOS_HKEY_MAX),
	 * or hashed key for variable-length/large key. In the later case,
	 * the hashed key can be used for efficient comparison.
	 *
	 * When BTR_FEAT_UINT_KEY is set, no key callbacks are used for
	 * comparisons.
	 *
	 * When BTR_FEAT_DIRECT_KEY is used, we store the mmid of the
	 * relevant leaf node for direct key comparison
	 */
	union {
		char			rec_hkey[0]; /* hashed key */
		uint64_t		rec_ukey[0]; /* uint key */
		TMMID(struct btr_node)	rec_node[0]; /* direct key */
	};
};

/**
 * Tree node.
 *
 * NB: could be PM data structure.
 */
struct btr_node {
	/** leaf, root etc */
	uint16_t			tn_flags;
	/** number of keys stored in this node */
	uint16_t			tn_keyn;
	/** padding bytes */
	uint32_t			tn_pad_32;
	/** generation, reserved for COW */
	uint64_t			tn_gen;
	/** the first child, it is unused on leaf node */
	TMMID(struct btr_node)		tn_child;
	/** records in this node */
	struct btr_record		tn_recs[0];
};

enum {
	BTR_ORDER_MIN			= 3,
	BTR_ORDER_MAX			= 4096
};

/**
 * Tree root descriptor, it consits of tree attributes and reference to the
 * actual root node.
 *
 * NB: could be PM data structure.
 */
struct btr_root {
	/** btree order */
	uint16_t			tr_order;
	/** depth of the tree */
	uint16_t			tr_depth;
	/**
	 * ID to find a registered tree class, which provides customized
	 * funtions etc.
	 */
	uint32_t			tr_class;
	/** the actual features of the tree, e.g. hash type, integer key */
	uint64_t			tr_feats;
	/** generation, reserved for COW */
	uint64_t			tr_gen;
	/** pointer to the root node, it is NULL for an empty tree */
	TMMID(struct btr_node)		tr_node;
};

/** btree attributes returned by query function. */
struct btr_attr {
	/** tree order */
	unsigned int			ba_order;
	/** tree depth */
	unsigned int			ba_depth;
	unsigned int			ba_class;
	uint64_t			ba_feats;
	/** memory class, pmem pool etc */
	struct umem_attr		ba_uma;
};

/** btree statistics returned by query function. */
struct btr_stat {
	/** total number of tree nodes */
	uint64_t			bs_node_nr;
	/** total number of records in the tree */
	uint64_t			bs_rec_nr;
	/** total number of bytes of all keys */
	uint64_t			bs_key_sum;
	/** max key size */
	uint64_t			bs_key_max;
	/** total number of bytes of all values */
	uint64_t			bs_val_sum;
	/** max value size */
	uint64_t			bs_val_max;
};

struct btr_rec_stat {
	/** record key size */
	uint64_t			rs_ksize;
	/** record value size */
	uint64_t			rs_vsize;
};

struct btr_instance;

typedef enum {
	/** probe a specific key */
	BTR_PROBE_SPEC		= (1 << 8),
	/**
	 * Require key/hkey compare function to return BTR_CMP_MATCHED for
	 * matched fetch and upsert.
	 */
	BTR_PROBE_MATCHED	= (1 << 9),
	/**
	 * Public probe opcodes, user can combine BTR_PROBE_MATCHED with any
	 * of the rest opcodes.
	 */
	/**
	 * unconditionally trust the probe result from the previous call,
	 * bypass probe process for dbtree_upsert (or delete) in the future.
	 *
	 * This can reduce search overhead for use cases like this:
	 *    rc = dbtree_fetch(...key...);
	 *    if (rc == -DER_NONEXIST) {
	 *	    do_something_else(...);
	 *	    rc = dbtree_upsert(..., BTR_PROBE_BYPASS, key...);
	 *    }
	 *
	 * Please be careful while using this flag, because it could break
	 * the correctness of dbtree if inserting a new key to a mismatched
	 * probe path.
	 */
	BTR_PROBE_BYPASS	= 0,
	/** the first record in the tree */
	BTR_PROBE_FIRST		= 1,
	/** the last record in the tree */
	BTR_PROBE_LAST		= 2,
	/** probe the record whose key equals to the provide key */
	BTR_PROBE_EQ		= BTR_PROBE_SPEC,
	/** probe the record whose key is great to the provided key */
	BTR_PROBE_GT		= BTR_PROBE_SPEC | 1,
	/** probe the record whose key is less to the provided key */
	BTR_PROBE_LT		= BTR_PROBE_SPEC | 2,
	/** probe the record whose key is great/equal to the provided key */
	BTR_PROBE_GE		= BTR_PROBE_SPEC | 3,
	/** probe the record whose key is less/equal to the provided key */
	BTR_PROBE_LE		= BTR_PROBE_SPEC | 4,
} dbtree_probe_opc_t;

/** the return value of to_hkey_cmp/to_key_cmp callback */
enum btr_key_cmp_rc {
	BTR_CMP_EQ	= (0),		/* equal */
	BTR_CMP_LT	= (1 << 0),	/* less than */
	BTR_CMP_GT	= (1 << 1),	/* greater than */
	/**
	 * User can return it combined with BTR_CMP_LT/GT. If it is set,
	 * dbtree can fetch/update value even the provided key is less/greater
	 * than the compared key.
	 */
	BTR_CMP_MATCHED	= (1 << 2),
	BTR_CMP_UNKNOWN	= (1 << 3),	/* unset */
	BTR_CMP_ERR	= (1 << 4),	/* error */
};

/**
 * Customized tree function table.
 */
typedef struct {
	/**
	 * Generate a fix-size hashed key from the real key.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param key	[IN]	key buffer
	 * \param hkey	[OUT]	hashed key
	 */
	void		(*to_hkey_gen)(struct btr_instance *tins,
				       daos_iov_t *key, void *hkey);
	/**
	 * Size of the hashed key.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 */
	int		(*to_hkey_size)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Comparison of hashed key.
	 *
	 * Absent:
	 * Calls memcmp.
	 *
	 * \param tins	[IN]		Tree instance which contains the root
	 *				mmid and memory class etc.
	 * \param rec	[IN]	Record to be compared with \a key.
	 * \param key	[IN]	Key to be compared with key of \a rec.
	 *
	 * \a return	BTR_CMP_LT	hkey of \a rec is smaller than \a hkey
	 *		BTR_CMP_GT	hkey of \a rec is larger than \a hkey
	 *		BTR_CMP_EQ	hkey of \a rec is equal to \a hkey
	 *		BTR_CMP_ERR	error in the hkey comparison
	 *		return any other value will cause assertion, segfault or
	 *		other undefined result.
	 */
	int		(*to_hkey_cmp)(struct btr_instance *tins,
				       struct btr_record *rec, void *hkey);
	/**
	 * Optional:
	 * Comparison of real key. It can be ignored if there is no hash
	 * for the key and key size is fixed. See \a btr_record for the details.
	 *
	 * Absent:
	 * Skip the function and only check rec::rec_hkey for the search.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be compared with \a key.
	 * \param key	[IN]	Key to be compared with key of \a rec.
	 *
	 * \a return	BTR_CMP_LT	key of \a rec is smaller than \a key
	 *		BTR_CMP_GT	key of \a rec is larger than \a key
	 *		BTR_CMP_EQ	key of \a rec is equal to \a key
	 *		BTR_CMP_ERR	error in the key comparison
	 *		return any other value will cause assertion, segfault or
	 *		other undefined result.
	 */
	int		(*to_key_cmp)(struct btr_instance *tins,
				      struct btr_record *rec, daos_iov_t *key);
	/**
	 * Allocate record body for \a key and \a val.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param key	[IN]	Key buffer
	 * \param val	[IN]	Value buffer, it could be either data blob,
	 *			or complex data structure that can be parsed
	 *			by the tree class.
	 * \param rec	[OUT]	Returned record body mmid,
	 *			See \a btr_record for the details.
	 */
	int		(*to_rec_alloc)(struct btr_instance *tins,
					daos_iov_t *key, daos_iov_t *val,
					struct btr_record *rec);
	/**
	 * Free the record body stored in \a rec::rec_mmid
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	The record to be destroyed.
	 * \param args	[OUT]
	 *			Optional: opaque buffer for providing arguments
	 *			to handle special cases for free. for example,
	 *			to return the freed record to the user
	 */
	int		(*to_rec_free)(struct btr_instance *tins,
				       struct btr_record *rec, void *args);
	/**
	 * Fetch value or both key & value of a record.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read from.
	 * \param key	[OUT]	Optional, sink buffer for the returned key,
	 *			or key address.
	 * \param val	[OUT]	Sink buffer for the returned value or the
	 *			value address.
	 */
	int		(*to_rec_fetch)(struct btr_instance *tins,
					struct btr_record *rec,
					daos_iov_t *key, daos_iov_t *val);
	/**
	 * Update value of a record, the new value should be stored in the
	 * current rec::rec_mmid.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read update.
	 * \param val	[IN]	New value to be stored for the record.
	 * \a return	0	success.
	 *		-DER_NO_PERM
	 *			cannot make inplace change, should call
	 *			rec_free() to release the original record
	 *			and rec_alloc() to create a new record.
	 *		-ve	error code
	 */
	int		(*to_rec_update)(struct btr_instance *tins,
					 struct btr_record *rec,
					 daos_iov_t *key, daos_iov_t *val);
	/**
	 * Optional:
	 * Return key and value size of the record.
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to get size from.
	 * \param rstat	[OUT]	Returned key & value size.
	 */
	int		(*to_rec_stat)(struct btr_instance *tins,
				       struct btr_record *rec,
				       struct btr_rec_stat *rstat);
	/**
	 * Convert record into readable string and store it in \a buf.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be converted.
	 * \param leaf	[IN]	Both key and value should be converted to
	 *			string if it is true, otherwise only the
	 *			hashed key will be converted to string.
	 *			(record of intermediate node),
	 * \param buf	[OUT]	Buffer to store the returned string.
	 * \param buf_len [IN]	Buffer length.
	 */
	char	       *(*to_rec_string)(struct btr_instance *tins,
					 struct btr_record *rec, bool leaf,
					 char *buf, int buf_len);
	/**
	 * Optional:
	 * Allocate an empty tree. Upper layer can have customized
	 * implementation to manange internal tree node and record cache etc.
	 *
	 * Absent:
	 * The common code will allocate the root and initialise the empty tree.
	 *
	 * \param tins	[IN/OUT]
	 *			Input  : Tree instance which includes memory
	 *				 class and customized tree operations.
	 *			output : root mmid
	 * \param feats	[IN]	Feature bits of this true.
	 * \param order	[IN]	Order of the tree.
	 */
	int		(*to_root_alloc)(struct btr_instance *tins,
					 uint64_t feats, unsigned int order);
	/**
	 * Optional:
	 * Free the empty tree, and internal caches.
	 *
	 * Absent:
	 * The common code will free the root.
	 *
	 * \param tins	[IN]	Tree instance to be destroyed.
	 */
	void		(*to_root_free)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Add tree root to current transaction.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid,
	 *			root address and memory class etc.
	 */
	int		(*to_root_tx_add)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Allocate tree node from internal cache.
	 *
	 * Absent:
	 * The common code will allocate the tree node.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid_p [OUT]
	 *			Returned mmid of the new node.
	 *
	 * Absent:
	 * The common code will allocate a new tree node.
	 */
	int		(*to_node_alloc)(struct btr_instance *tins,
					 TMMID(struct btr_node) *nd_mmid_p);
	/**
	 * Optional:
	 * Release the tree node for the internal cache.
	 *
	 * Absent:
	 * The common code will free the tree node.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid [IN]
	 *			Node mmid to be freed.
	 *
	 * Absent:
	 * The common code will free the tree node.
	 */
	void		(*to_node_free)(struct btr_instance *tins,
					TMMID(struct btr_node) nd_mmid);
	/**
	 * Optional: add \a nd_mmid to the current transaction.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid [IN]
	 *			Node mmid to be added to transaction.
	 */
	int		(*to_node_tx_add)(struct btr_instance *tins,
					  TMMID(struct btr_node) nd_mmid);
} btr_ops_t;

/**
 * Tree instance, it is instantiated while creating or opening a tree.
 */
struct btr_instance {
	/** instance of memory class for the tree */
	struct umem_instance		 ti_umm;
	/**
	 * NVMe free space tracking information used for NVMe record
	 * alloc & free.
	 */
	void				*ti_blks_info;
	/** root mmid */
	TMMID(struct btr_root)		 ti_root_mmid;
	/** root pointer */
	struct btr_root			*ti_root;
	/** Customized operations for the tree */
	btr_ops_t			*ti_ops;
};

/* Features are passed as 64-bit unsigned integer.   Only the bits below are
 * reserved.   A specific class can define its own bits to customize behavior.
 * For example, VOS can use bits to indicate the type of key comparison used
 * for user supplied key.   In general, using the upper bits is safer to avoid
 * conflicts in the future.
 */
enum btr_feats {
	/** Key is an unsigned integer.  Implies no hash or key callbacks */
	BTR_FEAT_UINT_KEY		= (1 << 0),
	/** Key is not hashed or stored by library.  User must provide
	 * to_key_cmp callback
	 */
	BTR_FEAT_DIRECT_KEY		= (1 << 1),
};

/**
 * Get the return code of to_hkey_cmp/to_key_cmp in case of success, for failure
 * case need to directly set it as BTR_CMP_ERR.
 */
static inline int
dbtree_key_cmp_rc(int rc)
{
	if (rc == 0)
		return BTR_CMP_EQ;
	else if (rc < 0)
		return BTR_CMP_LT;
	else
		return BTR_CMP_GT;
}

int  dbtree_class_register(unsigned int tree_class, uint64_t tree_feats,
			   btr_ops_t *ops);
int  dbtree_create(unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   TMMID(struct btr_root) *root_mmidp, daos_handle_t *toh);
int  dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
			   unsigned int tree_order, struct umem_attr *uma,
			   struct btr_root *root, daos_handle_t *toh);
int  dbtree_open(TMMID(struct btr_root) root_mmid, struct umem_attr *uma,
		 daos_handle_t *toh);
int  dbtree_open_inplace(struct btr_root *root, struct umem_attr *uma,
			 daos_handle_t *toh);
int  dbtree_open_inplace_ex(struct btr_root *root, struct umem_attr *uma,
			    void *info, daos_handle_t *toh);
int  dbtree_close(daos_handle_t toh);
int  dbtree_destroy(daos_handle_t toh);
int  dbtree_lookup(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val_out);
int  dbtree_update(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val);
int  dbtree_fetch(daos_handle_t toh, dbtree_probe_opc_t opc,
		  daos_iov_t *key, daos_iov_t *key_out, daos_iov_t *val_out);
int  dbtree_upsert(daos_handle_t toh, dbtree_probe_opc_t opc,
		   daos_iov_t *key, daos_iov_t *val);
int  dbtree_delete(daos_handle_t toh, daos_iov_t *key, void *args);
int  dbtree_query(daos_handle_t toh, struct btr_attr *attr,
		  struct btr_stat *stat);
int  dbtree_is_empty(daos_handle_t toh);

/******* iterator API ******************************************************/

enum {
	/**
	 * Use the embedded iterator of the open handle.
	 * It can reduce memory consumption, but state of iterator can be
	 * overwritten by other tree operation.
	 */
	BTR_ITER_EMBEDDED	= (1 << 0),
};

int dbtree_iter_prepare(daos_handle_t toh, unsigned int options,
			daos_handle_t *ih);
int dbtree_iter_finish(daos_handle_t ih);
int dbtree_iter_probe(daos_handle_t ih, dbtree_probe_opc_t opc,
		      daos_iov_t *key, daos_anchor_t *anchor);
int dbtree_iter_next(daos_handle_t ih);
int dbtree_iter_prev(daos_handle_t ih);
int dbtree_iter_fetch(daos_handle_t ih, daos_iov_t *key,
		      daos_iov_t *val, daos_anchor_t *anchor);
int dbtree_iter_delete(daos_handle_t ih, void *args);
int dbtree_iter_empty(daos_handle_t ih);

/**
 * Prototype of dbtree_iterate() callbacks. When a callback returns an rc,
 *
 *   - if rc == 0, dbtree_iterate() continues;
 *   - if rc == 1, dbtree_iterate() stops and returns 0;
 *   - otherwise, dbtree_iterate() stops and returns rc.
 */
typedef int (*dbtree_iterate_cb_t)(daos_handle_t ih, daos_iov_t *key,
				   daos_iov_t *val, void *arg);
int dbtree_iterate(daos_handle_t toh, bool backward, dbtree_iterate_cb_t cb,
		   void *arg);

enum {
	DBTREE_VOS_BEGIN	= 10,
	DBTREE_VOS_END		= DBTREE_VOS_BEGIN + 9,
	DBTREE_DSM_BEGIN	= 20,
	DBTREE_DSM_END		= DBTREE_DSM_BEGIN + 9,
	DBTREE_SMD_BEGIN	= 30,
	DBTREE_SMD_END		= DBTREE_SMD_BEGIN + 9,
};

#endif /* __DAOS_BTREE_H__ */
