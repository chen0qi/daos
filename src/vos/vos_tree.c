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
 * vos/vos_tree.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <daos_api.h> /* For ofeat bits */
#include "vos_internal.h"

#define VOS_KTR_ORDER		23	/* order of d/a-key tree */
#define VOS_SVT_ORDER		5	/* order of single value tree */
#define VOS_EVT_ORDER		23	/* evtree order */

/**
 * VOS Btree attributes, for tree registration and tree creation.
 */
struct vos_btr_attr {
	/** tree class ID */
	int		 ta_class;
	/** default tree order */
	int		 ta_order;
	/** feature bits */
	uint64_t	 ta_feats;
	/** name of tree type */
	char		*ta_name;
	/** customized tree functions */
	btr_ops_t	*ta_ops;
};

static struct vos_btr_attr *obj_tree_find_attr(unsigned tree_class);

static struct vos_key_bundle *
iov2key_bundle(daos_iov_t *key_iov)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct vos_key_bundle));
	return (struct vos_key_bundle *)key_iov->iov_buf;
}

static struct vos_rec_bundle *
iov2rec_bundle(daos_iov_t *val_iov)
{
	D_ASSERT(val_iov->iov_len == sizeof(struct vos_rec_bundle));
	return (struct vos_rec_bundle *)val_iov->iov_buf;
}

/**
 * @defgroup vos_key_btree vos key-btree
 * @{
 */

/**
 * hashed key for the key-btree, it is stored in btr_record::rec_hkey
 */
struct ktr_hkey {
	/** murmur64 hash */
	uint64_t		kh_hash[2];
	/** epoch when this key is punched. */
	uint64_t		kh_epoch;
	/** cacheline alignment */
	uint64_t		kh_pad_64;
};

/**
 * Store a key and its checksum as a durable struct.
 */
static int
ktr_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;
	char		   *kbuf;

	krec->kr_cs_size = csum->cs_len;
	if (krec->kr_cs_size != 0) {
		D_ASSERT(csum->cs_csum);
		krec->kr_cs_type = csum->cs_type;
		memcpy(vos_krec2csum(krec), csum->cs_csum, csum->cs_len);
	}
	kbuf = vos_krec2key(krec);

	if (iov->iov_buf != NULL) {
		D_ASSERT(iov->iov_buf == kbund->kb_key->iov_buf);
		memcpy(kbuf, iov->iov_buf, iov->iov_len);
	} else {
		/* return it for RDMA */
		iov->iov_buf = kbuf;
	}
	krec->kr_size = iov->iov_len;
	return 0;
}

/**
 * Copy key and its checksum stored in \a rec into external buffer if it's
 * provided, otherwise return memory address of key and checksum.
 */
static int
ktr_rec_load(struct btr_instance *tins, struct btr_record *rec,
	     struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;
	char		   *kbuf;

	kbuf = vos_krec2key(krec);
	iov->iov_len = krec->kr_size;
	if (iov->iov_buf == NULL) {
		iov->iov_buf = kbuf;
		iov->iov_buf_len = krec->kr_size;

	} else if (iov->iov_buf_len >= iov->iov_len) {
		memcpy(iov->iov_buf, kbuf, iov->iov_len);
	}

	csum->cs_len  = krec->kr_cs_size;
	csum->cs_type = krec->kr_cs_type;
	if (csum->cs_csum == NULL)
		csum->cs_csum = vos_krec2csum(krec);
	else if (csum->cs_buf_len > csum->cs_len)
		memcpy(csum->cs_csum, vos_krec2csum(krec), csum->cs_len);

	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
ktr_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct ktr_hkey);
}

/** generate hkey */
static void
ktr_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct ktr_hkey		*kkey  = (struct ktr_hkey *)hkey;
	struct vos_key_bundle	*kbund = iov2key_bundle(key_iov);
	daos_key_t		*key   = kbund->kb_key;

	kkey->kh_hash[0] = d_hash_murmur64(key->iov_buf, key->iov_len,
					   VOS_BTR_MUR_SEED);
	kkey->kh_hash[1] = d_hash_string_u32(key->iov_buf, key->iov_len);
	kkey->kh_epoch	 = kbund->kb_epoch;
}

/** compare the hashed key */
static int
ktr_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct ktr_hkey *k1 = (struct ktr_hkey *)&rec->rec_hkey[0];
	struct ktr_hkey *k2 = (struct ktr_hkey *)hkey;

	if (k1->kh_hash[0] < k2->kh_hash[0])
		return BTR_CMP_LT;

	if (k1->kh_hash[0] > k2->kh_hash[0])
		return BTR_CMP_GT;

	if (k1->kh_hash[1] < k2->kh_hash[1])
		return BTR_CMP_LT;

	if (k1->kh_hash[1] > k2->kh_hash[1])
		return BTR_CMP_GT;

	if (k1->kh_epoch > k2->kh_epoch)
		return BTR_CMP_GT | BTR_CMP_MATCHED;

	if (k1->kh_epoch < k2->kh_epoch)
		return BTR_CMP_LT | BTR_CMP_MATCHED;

	return BTR_CMP_EQ;
}

static int
ktr_key_cmp_lexical(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	int cmp;

	/* First, compare the bytes */
	cmp = memcmp(vos_krec2key(krec), (char *)kiov->iov_buf,
		     min(krec->kr_size, kiov->iov_len));
	if (cmp)
		return dbtree_key_cmp_rc(cmp);

	/* Second, fallback to the length */
	if (krec->kr_size > kiov->iov_len)
		return BTR_CMP_GT;
	else if (krec->kr_size < kiov->iov_len)
		return BTR_CMP_LT;

	return BTR_CMP_EQ;
}

static int
ktr_key_cmp_uint64(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	uint64_t k1, k2;

	if (krec->kr_size != kiov->iov_len ||
	    krec->kr_size != sizeof(uint64_t)) {
		D_ERROR("invalid kr_size %d.\n", krec->kr_size);
		return BTR_CMP_ERR;
	}

	k1 = *(uint64_t *)vos_krec2key(krec);
	k2 = *(uint64_t *)kiov->iov_buf;

	return (k1 > k2) ? BTR_CMP_GT :
			   ((k1 < k2) ? BTR_CMP_LT : BTR_CMP_EQ);
}

static int
ktr_key_cmp_default(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	/* This only gets called if hash comparison matches. */
	if (krec->kr_size > kiov->iov_len)
		return BTR_CMP_GT;

	if (krec->kr_size < kiov->iov_len)
		return BTR_CMP_LT;

	return dbtree_key_cmp_rc(
		memcmp(vos_krec2key(krec), kiov->iov_buf, kiov->iov_len));
}

/** compare the real key */
static int
ktr_key_cmp(struct btr_instance *tins, struct btr_record *rec,
	    daos_iov_t *key_iov)
{
	daos_iov_t		*kiov;
	struct vos_krec_df	*krec;
	struct vos_key_bundle	*kbund;
	uint64_t		 feats = tins->ti_root->tr_feats;
	int			 cmp = 0;

	krec  = vos_rec2krec(tins, rec);
	kbund = iov2key_bundle(key_iov);
	kiov  = kbund->kb_key;

	if (feats & VOS_KEY_CMP_UINT64)
		cmp = ktr_key_cmp_uint64(krec, kiov);
	else if (feats & VOS_KEY_CMP_LEXICAL)
		cmp = ktr_key_cmp_lexical(krec, kiov);
	else
		cmp = ktr_key_cmp_default(krec, kiov);

	if (cmp != BTR_CMP_EQ)
		return cmp;

	if (krec->kr_punched > kbund->kb_epoch)
		return BTR_CMP_GT | BTR_CMP_MATCHED;

	if (krec->kr_punched < kbund->kb_epoch)
		return BTR_CMP_LT | BTR_CMP_MATCHED;

	return BTR_CMP_EQ;
}

/** create a new key-record, or install an externally allocated key-record */
static int
ktr_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	struct vos_btr_attr	*ta;
	struct umem_attr	 uma;
	daos_handle_t		 btr_oh = DAOS_HDL_INVAL;
	daos_handle_t		 evt_oh = DAOS_HDL_INVAL;
	uint64_t		 tree_feats = 0;
	int			 rc;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

	rec->rec_mmid = umem_zalloc(&tins->ti_umm,
				    vos_krec_size(rbund->rb_tclass, rbund));
	if (UMMID_IS_NULL(rec->rec_mmid))
		return -DER_NOMEM;

	krec = vos_rec2krec(tins, rec);
	krec->kr_punched = kbund->kb_epoch;
	rbund->rb_krec = krec;

	/* Step-1: find the btree attributes and create btree */
	ta = obj_tree_find_attr(tins->ti_root->tr_class);
	D_ASSERTF(ta != NULL, "gets NULL ta for tr_class %d.\n",
		 tins->ti_root->tr_class);

	D_DEBUG(DB_TRACE, "Create dbtree %s\n", ta->ta_name);
	if (rbund->rb_tclass == VOS_BTR_DKEY) {
		uint64_t	obj_feats;

		/* Check and setup the akey key compare bits */
		obj_feats = tins->ti_root->tr_feats & VOS_OFEAT_MASK;
		obj_feats = obj_feats >> VOS_OFEAT_SHIFT;
		/* Use hashed key if feature bits aren't set for object */
		tree_feats = obj_feats << VOS_OFEAT_SHIFT;
		if (obj_feats & DAOS_OF_AKEY_UINT64)
			tree_feats |= VOS_KEY_CMP_UINT64_SET;
		else if (obj_feats & DAOS_OF_AKEY_LEXICAL)
			tree_feats |= VOS_KEY_CMP_LEXICAL_SET;
	}

	umem_attr_get(&tins->ti_umm, &uma);
	rc = dbtree_create_inplace(ta->ta_class, tree_feats, ta->ta_order,
				   &uma, &krec->kr_btr, &btr_oh);
	if (rc != 0) {
		D_ERROR("Failed to create btree: %d\n", rc);
		return rc;
	}

	/* Step-2: create evtree for akey only */
	if (rbund->rb_tclass == VOS_BTR_AKEY) {
		D_DEBUG(DB_TRACE, "Create evtree\n");

		krec->kr_bmap |= KREC_BF_EVT;
		rc = evt_create_inplace(EVT_FEAT_DEFAULT, VOS_EVT_ORDER, &uma,
					&krec->kr_evt[0], &evt_oh);
		if (rc != 0) {
			D_ERROR("Failed to create evtree: %d\n", rc);
			D_GOTO(out, rc);
		}
	}
	ktr_rec_store(tins, rec, kbund, rbund);
 out:
	if (!daos_handle_is_inval(btr_oh))
		dbtree_close(btr_oh);

	if (!daos_handle_is_inval(evt_oh))
		evt_close(evt_oh);

	return rc;
}

static int
ktr_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct vos_krec_df *krec;
	struct umem_attr    uma;
	daos_handle_t	    toh;
	int		    rc = 0;

	if (UMMID_IS_NULL(rec->rec_mmid))
		return 0;

	krec = vos_rec2krec(tins, rec);
	umem_attr_get(&tins->ti_umm, &uma);

	/* has subtree? */
	if (krec->kr_btr.tr_order) {
		rc = dbtree_open_inplace_ex(&krec->kr_btr, &uma,
					    tins->ti_blks_info, &toh);
		if (rc != 0)
			D_ERROR("Failed to open btree: %d\n", rc);
		else
			dbtree_destroy(toh);
	}

	if ((krec->kr_bmap & KREC_BF_EVT) && krec->kr_evt[0].tr_order) {
		rc = evt_open_inplace(&krec->kr_evt[0], &uma,
				      tins->ti_blks_info, &toh);
		if (rc != 0)
			D_ERROR("Failed to open evtree: %d\n", rc);
		else
			evt_destroy(toh);
	}
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return rc;
}

static int
ktr_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	struct vos_rec_bundle	*rbund;
	struct vos_key_bundle	*kbund;

	rbund = iov2rec_bundle(val_iov);
	rbund->rb_krec = krec;

	if (key_iov != NULL) {
		kbund = iov2key_bundle(key_iov);
		kbund->kb_epoch = krec->kr_punched;

		ktr_rec_load(tins, rec, kbund, rbund);
	}
	return 0;
}

static int
ktr_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);

	rbund->rb_krec = vos_rec2krec(tins, rec);
	/* NB: do nothing at here except return the sub-tree root,
	 * because the real update happens in the sub-tree (index &
	 * epoch tree).
	 */
	return 0;
}

static btr_ops_t key_btr_ops = {
	.to_hkey_size		= ktr_hkey_size,
	.to_hkey_gen		= ktr_hkey_gen,
	.to_hkey_cmp		= ktr_hkey_cmp,
	.to_key_cmp		= ktr_key_cmp,
	.to_rec_alloc		= ktr_rec_alloc,
	.to_rec_free		= ktr_rec_free,
	.to_rec_fetch		= ktr_rec_fetch,
	.to_rec_update		= ktr_rec_update,
};

/**
 * @} vos_key_btree
 */

/**
 * @defgroup vos_singv_btr vos single value btree
 * @{
 */

struct svt_hkey {
	/** */
	uint64_t	sv_epoch;
	/** cookie ID tag for this update */
	uuid_t		sv_cookie;
};

/**
 * Set size for the record and returns write buffer address of the record,
 * so caller can copy/rdma data into it.
 */
static int
svt_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_irec_df	*irec	= vos_rec2irec(tins, rec);
	daos_csum_buf_t		*csum	= rbund->rb_csum;
	struct eio_iov		*eiov	= rbund->rb_eiov;
	struct svt_hkey		*skey;

	if (eiov->ei_data_len != rbund->rb_rsize)
		return -DER_IO_INVAL;

	skey = (struct svt_hkey *)&rec->rec_hkey[0];
	/** Updating the cookie for this update */
	uuid_copy(skey->sv_cookie, rbund->rb_cookie);

	/** XXX: fix this after CSUM is added to iterator */
	irec->ir_cs_size = csum->cs_len;
	irec->ir_cs_type = csum->cs_type;
	irec->ir_size	 = eiov->ei_data_len;
	irec->ir_ex_addr = eiov->ei_addr;
	irec->ir_ver	 = rbund->rb_ver;

	if (irec->ir_size == 0) { /* it is a punch */
		csum->cs_csum = NULL;
		return 0;
	}

	csum->cs_csum = vos_irec2csum(irec);
	return 0;
}

/**
 * Return memory address of data and checksum of this record.
 */
static int
svt_rec_load(struct btr_instance *tins, struct btr_record *rec,
	     struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct svt_hkey    *skey = (struct svt_hkey *)&rec->rec_hkey[0];
	struct vos_irec_df *irec = vos_rec2irec(tins, rec);
	daos_csum_buf_t    *csum = rbund->rb_csum;
	struct eio_iov     *eiov = rbund->rb_eiov;

	if (kbund != NULL) /* called from iterator */
		kbund->kb_epoch = skey->sv_epoch;

	uuid_copy(rbund->rb_cookie, skey->sv_cookie);

	/* NB: return record address, caller should copy/rma data for it */
	eiov->ei_data_len = irec->ir_size;
	eiov->ei_addr = irec->ir_ex_addr;
	eiov->ei_buf = NULL;

	if (irec->ir_size != 0) {
		csum->cs_len	= irec->ir_cs_size;
		csum->cs_type	= irec->ir_cs_type;
		csum->cs_csum	= vos_irec2csum(irec);
	}

	rbund->rb_rsize	= irec->ir_size;
	rbund->rb_ver	= irec->ir_ver;
	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
svt_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct svt_hkey);
}

/** generate hkey */
static void
svt_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct svt_hkey		*skey = (struct svt_hkey *)hkey;
	struct vos_key_bundle	*kbund;

	kbund = iov2key_bundle(key_iov);
	skey->sv_epoch = kbund->kb_epoch;
}

/** compare the hashed key */
static int
svt_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct svt_hkey *skey1 = (struct svt_hkey *)&rec->rec_hkey[0];
	struct svt_hkey *skey2 = (struct svt_hkey *)hkey;

	if (skey1->sv_epoch < skey2->sv_epoch)
		return BTR_CMP_LT;

	if (skey1->sv_epoch > skey2->sv_epoch)
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

/** allocate a new record and fetch data */
static int
svt_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_rec_bundle	*rbund;
	struct vos_key_bundle	*kbund;
	int			 rc = 0;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

	if (UMMID_IS_NULL(rbund->rb_mmid)) {
		rec->rec_mmid = umem_alloc(&tins->ti_umm,
					   vos_irec_size(rbund));
		if (UMMID_IS_NULL(rec->rec_mmid))
			return -DER_NOMEM;
	} else {
		rec->rec_mmid = rbund->rb_mmid;
		rbund->rb_mmid = UMMID_NULL; /* taken over by btree */
	}

	rc = svt_rec_store(tins, rec, kbund, rbund);
	return rc;
}

static int
svt_rec_free(struct btr_instance *tins, struct btr_record *rec,
	      void *args)
{
	struct vos_irec_df *irec = vos_rec2irec(tins, rec);
	eio_addr_t *addr = &irec->ir_ex_addr;

	if (UMMID_IS_NULL(rec->rec_mmid))
		return 0;

	if (args != NULL) {
		*(umem_id_t *)args = rec->rec_mmid;
		rec->rec_mmid = UMMID_NULL; /** taken over by user */
		return 0;
	}

	if (addr->ea_type == EIO_ADDR_NVME && !eio_addr_is_hole(addr)) {
		struct vea_space_info *vsi = tins->ti_blks_info;
		uint64_t blk_off;
		uint32_t blk_cnt;
		int rc;

		D_ASSERT(vsi != NULL);

		blk_off = vos_byte2blkoff(addr->ea_off);
		blk_cnt = vos_byte2blkcnt(irec->ir_size);

		rc = vea_free(vsi, blk_off, blk_cnt);
		if (rc)
			D_ERROR("Error on block free. %d\n", rc);
	}

	umem_free(&tins->ti_umm, rec->rec_mmid);
	return 0;
}

static int
svt_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_key_bundle	*kbund = NULL;
	struct vos_rec_bundle	*rbund;

	rbund = iov2rec_bundle(val_iov);
	if (key_iov != NULL)
		kbund = iov2key_bundle(key_iov);

	svt_rec_load(tins, rec, kbund, rbund);
	return 0;
}

static int
svt_rec_update(struct btr_instance *tins, struct btr_record *rec,
		daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct svt_hkey		*skey;
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

	if (!UMMID_IS_NULL(rbund->rb_mmid) ||
	    !vos_irec_size_equal(vos_rec2irec(tins, rec), rbund)) {
		/* This function should return -DER_NO_PERM to dbtree if:
		 * - it is a rdma, the original record should be replaced.
		 * - the new record size cannot match the original one, so we
		 *   need to realloc and copyin data to the new space.
		 *
		 * So dbtree can release the original record and install the
		 * rdma-ed record, or just allocate a new one.
		 */
		return -DER_NO_PERM;
	}

	skey = (struct svt_hkey *)&rec->rec_hkey[0];
	D_DEBUG(DB_IO, "Overwrite epoch "DF_U64"\n", skey->sv_epoch);

	umem_tx_add(&tins->ti_umm, rec->rec_mmid, vos_irec_size(rbund));
	return svt_rec_store(tins, rec, kbund, rbund);
}

static btr_ops_t singv_btr_ops = {
	.to_hkey_size		= svt_hkey_size,
	.to_hkey_gen		= svt_hkey_gen,
	.to_hkey_cmp		= svt_hkey_cmp,
	.to_rec_alloc		= svt_rec_alloc,
	.to_rec_free		= svt_rec_free,
	.to_rec_fetch		= svt_rec_fetch,
	.to_rec_update		= svt_rec_update,
};

/**
 * @} vos_singv_btr
 */
static struct vos_btr_attr vos_btr_attrs[] = {
	{
		.ta_class	= VOS_BTR_DKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= VOS_OFEAT_BITS | BTR_FEAT_DIRECT_KEY,
		.ta_name	= "vos_dkey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_AKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= VOS_OFEAT_BITS | BTR_FEAT_DIRECT_KEY,
		.ta_name	= "vos_akey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_SINGV,
		.ta_order	= VOS_SVT_ORDER,
		.ta_feats	= 0,
		.ta_name	= "singv",
		.ta_ops		= &singv_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_END,
		.ta_name	= "null",
	},
};

/**
 * Load the subtree roots embedded in the parent tree record.
 *
 * akey tree	: all akeys under the same dkey
 * recx tree	: all record extents under the same akey, this function will
 *		  load both btree and evtree root.
 */
int
key_tree_prepare(struct vos_object *obj, daos_epoch_t epoch,
		 daos_handle_t toh, enum vos_tree_class tclass,
		 daos_key_t *key, int flags, daos_handle_t *sub_toh)
{
	struct umem_attr	*uma = vos_obj2uma(obj);
	struct vos_krec_df	*krec;
	daos_csum_buf_t		 csum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	struct vea_space_info	*info;
	int			 rc;

	D_DEBUG(DB_IO, "prepare tree, flags=%x, tclass=%d\n", flags, tclass);
	if (tclass != VOS_BTR_AKEY && (flags & SUBTR_EVT))
		D_GOTO(out, rc = -DER_INVAL);

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= key;
	kbund.kb_epoch	= epoch;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_mmid	= UMMID_NULL;
	rbund.rb_csum	= &csum;
	rbund.rb_tclass	= tclass;
	memset(&csum, 0, sizeof(csum));

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested, instead:
	 *
	 * - In the case of fetch, we load the subtree root stored in the
	 *   parent tree leaf.
	 * - In the case of update/insert, we call dbtree_update() which may
	 *   create the root for the subtree, or just return it if it's already
	 *   there.
	 */
	rc = dbtree_fetch(toh, BTR_PROBE_GE | BTR_PROBE_MATCHED, &kiov, NULL,
			  &riov);
	switch (rc) {
	default:
		D_ERROR("fetch failed: %d\n", rc);
		goto out;

	case -DER_NONEXIST:
		if (!(flags & SUBTR_CREATE))
			goto out;

		kbund.kb_epoch	= DAOS_EPOCH_MAX;
		rbund.rb_iov	= key;
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, &kiov, &riov);
		if (rc) {
			D_ERROR("Failed to upsert: %d\n", rc);
			goto out;
		}
		krec = rbund.rb_krec;
		break;
	case 0:
		krec = rbund.rb_krec;
		if (krec->kr_punched == epoch && epoch != DAOS_EPOCH_MAX) {
			/* already punched in this epoch */
			rc = -DER_NONEXIST;
			goto out;
		}
		break;
	}

	info = obj->obj_cont->vc_pool->vp_vea_info;
	if (flags & SUBTR_EVT) {
		rc = evt_open_inplace(&krec->kr_evt[0], uma, info, sub_toh);
		if (rc != 0)
			D_GOTO(out, rc);
	} else {
		rc = dbtree_open_inplace_ex(&krec->kr_btr, uma, info, sub_toh);
		if (rc != 0)
			D_GOTO(out, rc);
	}
 out:
	return rc;
}

/** Close the opened trees */
void
key_tree_release(daos_handle_t toh, bool is_array)
{
	int	rc;

	if (is_array)
		rc = evt_close(toh);
	else
		rc = dbtree_close(toh);

	D_ASSERT(rc == 0 || rc == -DER_NO_HDL);
}

/**
 * Punch a key in its parent tree.
 */
int
key_tree_punch(struct vos_object *obj, daos_handle_t toh, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, int flags)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	int			 rc;
	bool			 replay = (flags & VOS_OF_REPLAY_PC);

	rc = dbtree_fetch(toh, BTR_PROBE_GE | BTR_PROBE_MATCHED, key_iov,
			  NULL, val_iov);
	if (rc != 0) {
		D_ASSERT(rc == -DER_NONEXIST);
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, key_iov, val_iov);
		if (rc)
			D_ERROR("Failed to add new punch, rc=%d\n", rc);

		return rc;
	}

	/* found a match */
	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);
	krec = rbund->rb_krec;

	if (krec->kr_punched == kbund->kb_epoch)
		return 0; /* nothing to do */

	if (krec->kr_punched != DAOS_EPOCH_MAX && !replay) {
		D_CRIT("Underwrite is only for replay\n");
		return -DER_NO_PERM;
	}

	/* PROBE_EQ == insert in this case */
	rc = dbtree_upsert(toh, BTR_PROBE_EQ, key_iov, val_iov);
	if (rc)
		return rc;

	if (!replay) { /* delete the max epoch */
		struct umem_instance	*umm = vos_obj2umm(obj);
		struct vos_krec_df	*krec2;
		struct vos_key_bundle	 kbund2;
		daos_iov_t	         tmp;

		krec2 = rbund->rb_krec;
		krec2->kr_btr = krec->kr_btr;
		umem_tx_add_ptr(umm, &krec->kr_btr, sizeof(krec->kr_btr));
		memset(&krec->kr_btr, 0, sizeof(krec->kr_btr));

		if (krec->kr_bmap & KREC_BF_EVT) {
			krec2->kr_evt[0] = krec->kr_evt[0];
			umem_tx_add_ptr(umm, &krec->kr_evt[0],
					sizeof(krec->kr_evt[0]));
			memset(&krec->kr_evt[0], 0, sizeof(krec->kr_evt[0]));
		}
		tree_key_bundle2iov(&kbund2, &tmp);
		kbund2.kb_key	= kbund->kb_key;
		kbund2.kb_epoch	= DAOS_EPOCH_MAX;

		rc = dbtree_delete(toh, &tmp, NULL);
		if (rc)
			D_ERROR("Failed to delete: %d\n", rc);
	}
	return rc;
}

/** initialize tree for an object */
int
obj_tree_init(struct vos_object *obj)
{
	struct vos_btr_attr *ta	= &vos_btr_attrs[0];
	int			rc;

	if (!daos_handle_is_inval(obj->obj_toh))
		return 0;

	D_ASSERT(obj->obj_df);
	if (obj->obj_df->vo_tree.tr_class == 0) {
		uint64_t	tree_feats	= 0;
		daos_ofeat_t	obj_feats;

		D_DEBUG(DB_DF, "Create btree for object\n");

		obj_feats = daos_obj_id2feat(obj->obj_df->vo_id.id_pub);
		/* Use hashed key if feature bits aren't set for object */
		tree_feats = (uint64_t)obj_feats << VOS_OFEAT_SHIFT;
		if (obj_feats & DAOS_OF_DKEY_UINT64)
			tree_feats |= VOS_KEY_CMP_UINT64_SET;
		else if (obj_feats & DAOS_OF_DKEY_LEXICAL)
			tree_feats |= VOS_KEY_CMP_LEXICAL_SET;

		rc = dbtree_create_inplace(ta->ta_class, tree_feats,
					   ta->ta_order, vos_obj2uma(obj),
					   &obj->obj_df->vo_tree,
					   &obj->obj_toh);
	} else {
		D_DEBUG(DB_DF, "Open btree for object\n");
		rc = dbtree_open_inplace(&obj->obj_df->vo_tree,
					 vos_obj2uma(obj), &obj->obj_toh);
	}
	return rc;
}

/** close btree for an object */
int
obj_tree_fini(struct vos_object *obj)
{
	int	rc = 0;

	/* NB: tree is created inplace, so don't need to destroy */
	if (!daos_handle_is_inval(obj->obj_toh)) {
		D_ASSERT(obj->obj_df);
		rc = dbtree_close(obj->obj_toh);
		obj->obj_toh = DAOS_HDL_INVAL;
	}
	return rc;
}

/** register all tree classes for VOS. */
int
obj_tree_register(void)
{
	struct vos_btr_attr *ta;
	int		     rc = 0;

	for (ta = &vos_btr_attrs[0]; ta->ta_class != VOS_BTR_END; ta++) {
		rc = dbtree_class_register(ta->ta_class, ta->ta_feats,
					   ta->ta_ops);
		if (rc != 0) {
			D_ERROR("Failed to register %s: %d\n", ta->ta_name, rc);
			break;
		}
		D_DEBUG(DB_TRACE, "Register tree type %s\n", ta->ta_name);
	}
	return rc;
}

/** find the attributes of the subtree of @tree_class */
static struct vos_btr_attr *
obj_tree_find_attr(unsigned tree_class)
{
	int	i;

	switch (tree_class) {
	default:
	case VOS_BTR_SINGV:
		return NULL;

	case VOS_BTR_AKEY:
		tree_class = VOS_BTR_SINGV;
		break;

	case VOS_BTR_DKEY:
		/* TODO: change it to VOS_BTR_AKEY while adding akey support */
		tree_class = VOS_BTR_AKEY;
		break;
	}

	for (i = 0;; i++) {
		struct vos_btr_attr *ta = &vos_btr_attrs[i];

		D_DEBUG(DB_TRACE, "ta->ta_class: %d, tree_class: %d\n",
			ta->ta_class, tree_class);

		if (ta->ta_class == tree_class)
			return ta;

		if (ta->ta_class == VOS_BTR_END)
			return NULL;
	}
}
