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
 * This file is part of daos
 *
 * tests/suite/daos_capa.c
 */
#define DD_SUBSYS	DD_FAC(tests)
#include "daos_test.h"

void
poh_invalidate_local(daos_handle_t *poh)
{
	daos_iov_t	ghdl = { NULL, 0, 0 };
	int		rc;

	/** fetch size of global handle */
	rc = daos_pool_local2global(*poh, &ghdl);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	/** generate global handle */
	rc = daos_pool_local2global(*poh, &ghdl);
	assert_int_equal(rc, 0);

	/** close local handle */
	rc = daos_pool_disconnect(*poh, NULL);
	assert_int_equal(rc, 0);

	/** recreate it ... although it is not valid on the server */
	rc = daos_pool_global2local(ghdl, poh);
	assert_int_equal(rc, 0);
}

/** query with invalid pool handle */
static void
query(void **state)
{
	test_arg_t		*arg = *state;
	daos_pool_info_t	 info;
	daos_handle_t		 poh;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** query pool info with valid handle */
	print_message("querying pool with valid handle ...\n");
	rc = daos_pool_query(poh, NULL, &info, NULL);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** query pool info with invalid handle */
	print_message("querying pool with invalid handle ...\n");
	rc = daos_pool_query(poh, NULL, &info, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** create container with invalid pool handle */
static void
create(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	uuid_t			 uuid;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** create container with read-only handle */
	print_message("creating container with read-only pool handle ...\n");
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** close local RO handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** create container with invalid handle */
	print_message("creating container with stale pool handle ...\n");
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** destroy container with invalid pool handle */
static void
destroy(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	uuid_t			 uuid;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** create container */
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** destroy container with invalid handle */
	print_message("destroying container with stale pool handle ...\n");
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** destroy container with RO handle */
	print_message("destroying container with read-only pool handle ...\n");
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** close local RO handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** destroy container with valid handle */
	print_message("really destroying container with valid pool handle "
		      "...\n");
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, 0);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** open container with invalid pool handle */
static void
open(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	daos_handle_t		 coh;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** invalidate pool handle */
	poh_invalidate_local(&poh);

	/** open container while pool handle has been revoked */
	print_message("opening container with revoked pool handle ...\n");
	rc = daos_cont_open(poh, arg->co_uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close pool handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** reconnect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, arg->group,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** open container in read/write mode */
	print_message("opening container RW with RO pool handle ...\n");
	rc = daos_cont_open(poh, arg->co_uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** invalidate pool handle */
	poh_invalidate_local(&poh);

	/** open container while pool handle has been revoked */
	rc = daos_cont_open(poh, arg->co_uuid, DAOS_COO_RO, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_HDL);


	/** close pool handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

#define STACK_BUF_LEN	24

/** update/fetch with invalid pool handle */
static void
io_invalid_poh(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	daos_handle_t		 coh;
	daos_obj_id_t		 oid;
	daos_handle_t		 oh;
	daos_epoch_t		 epoch = time(NULL);
	daos_iov_t		 dkey;
	daos_sg_list_t		 sgl;
	daos_iov_t		 sg_iov;
	daos_vec_iod_t		 iod;
	daos_recx_t		 recx;
	char			 buf[STACK_BUF_LEN];
	int			 rc;

	if (arg->rank_size < 2)
		skip();

	if (arg->myrank == 0) {
		/** connect to the pool in read-write mode */
		rc = daos_pool_connect(arg->pool_uuid, arg->group,
				       &arg->svc, DAOS_PC_RW, &poh,
				       NULL /* info */,
				       NULL /* ev */);
		assert_int_equal(rc, 0);
	}

	handle_share(&poh, HANDLE_POOL, arg->myrank, poh, false);

	if (arg->myrank == 1) {
		/** open container in read/write mode */
		rc = daos_cont_open(poh, arg->co_uuid, DAOS_COO_RW, &coh, NULL,
				    NULL);
		assert_int_equal(rc, 0);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank != 1) {
		rc = daos_pool_disconnect(poh, NULL);
		assert_int_equal(rc, 0);
		print_message("invalidating pool handle\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 1) {
		/** open object */
		oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
		rc = daos_obj_open(coh, oid, 0, 0, &oh, NULL);
		assert_int_equal(rc, 0);

		/** init I/O */
		daos_iov_set(&dkey, "dkey", strlen("dkey"));
		daos_iov_set(&sg_iov, buf, sizeof(buf));
		sgl.sg_nr.num		= 1;
		sgl.sg_nr.num_out	= 0;
		sgl.sg_iovs		= &sg_iov;
		daos_iov_set(&iod.vd_name, "akey", strlen("akey"));
		daos_csum_set(&iod.vd_kcsum, NULL, 0);
		iod.vd_nr	= 1;
		recx.rx_rsize	= 1;
		recx.rx_idx	= 0;
		recx.rx_nr	= sizeof(buf);
		iod.vd_recxs	= &recx;
		iod.vd_eprs	= NULL;
		iod.vd_csums	= NULL;

		/** update record */
		print_message("Updating %d bytes with invalid pool handle ..."
			      "\n", STACK_BUF_LEN);
		rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
		assert_int_equal(rc, -DER_NO_HDL);
		print_message("got -DER_NO_HDL as expected\n");

		/** fetch */
		print_message("fetching records with invalid pool handle...\n");
		rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL,
				    NULL);
		assert_int_equal(rc, -DER_NO_HDL);
		print_message("got -DER_NO_HDL as expected\n");

		/** close object */
		rc = daos_obj_close(oh, NULL);
		assert_int_equal(rc, 0);

		/** close container handle */
		rc = daos_cont_close(coh, NULL);
		assert_int_equal(rc, 0);

		/** close local pool handle */
		rc = daos_pool_disconnect(poh, NULL);
		assert_int_equal(rc, 0);
		print_message("all is fine\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

/** update/fetch with invalid container handle */
static void
io_invalid_coh(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 coh;
	daos_obj_id_t		 oid;
	daos_handle_t		 oh;
	daos_epoch_t		 epoch = time(NULL);
	daos_iov_t		 dkey;
	daos_sg_list_t		 sgl;
	daos_iov_t		 sg_iov;
	daos_vec_iod_t		 iod;
	daos_recx_t		 recx;
	char			 buf[STACK_BUF_LEN];
	int			 rc;

	if (arg->rank_size < 2)
		skip();

	if (arg->myrank == 0) {
		/** open container in read/write mode */
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW, &coh,
				    NULL, NULL);
		assert_int_equal(rc, 0);
	}

	handle_share(&coh, HANDLE_CO, arg->myrank, arg->poh, false);

	if (arg->myrank != 1) {
		rc = daos_cont_close(coh, NULL);
		assert_int_equal(rc, 0);
		print_message("closing container handle\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 1) {
		/** open object */
		oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
		rc = daos_obj_open(coh, oid, 0, 0, &oh, NULL);
		assert_int_equal(rc, 0);

		/** init I/O */
		daos_iov_set(&dkey, "dkey", strlen("dkey"));
		daos_iov_set(&sg_iov, buf, sizeof(buf));
		sgl.sg_nr.num		= 1;
		sgl.sg_nr.num_out	= 0;
		sgl.sg_iovs		= &sg_iov;
		daos_iov_set(&iod.vd_name, "akey", strlen("akey"));
		daos_csum_set(&iod.vd_kcsum, NULL, 0);
		iod.vd_nr	= 1;
		recx.rx_rsize	= 1;
		recx.rx_idx	= 0;
		recx.rx_nr	= sizeof(buf);
		iod.vd_recxs	= &recx;
		iod.vd_eprs	= NULL;
		iod.vd_csums	= NULL;

		/** update record */
		print_message("Updating records with stale container handle "
			      "...\n");
		rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
		assert_int_equal(rc, -DER_NO_HDL);
		print_message("got -DER_NO_HDL as expected\n");

		/** fetch */
		print_message("fetching records with stale container handle "
			      "...\n");
		rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL,
				    NULL);
		assert_int_equal(rc, -DER_NO_HDL);
		print_message("got -DER_NO_HDL as expected\n");

		/** close object */
		rc = daos_obj_close(oh, NULL);
		assert_int_equal(rc, 0);

		/** close container handle */
		rc = daos_cont_close(coh, NULL);
		assert_int_equal(rc, 0);
		print_message("all is fine\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

/** update with read-only container handle */
static void
update_ro(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 coh;
	daos_obj_id_t		 oid;
	daos_handle_t		 oh;
	daos_epoch_t		 epoch = time(NULL);
	daos_iov_t		 dkey;
	daos_sg_list_t		 sgl;
	daos_iov_t		 sg_iov;
	daos_vec_iod_t		 iod;
	daos_recx_t		 recx;
	char			 buf[STACK_BUF_LEN];
	int			 rc;

	if (arg->rank_size < 2)
		skip();

	if (arg->myrank == 0) {
		/** open container in read/write mode */
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RO, &coh,
				    NULL, NULL);
		assert_int_equal(rc, 0);
	}

	handle_share(&coh, HANDLE_CO, arg->myrank, arg->poh, false);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init I/O */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr.num		= 1;
	sgl.sg_nr.num_out	= 0;
	sgl.sg_iovs		= &sg_iov;
	daos_iov_set(&iod.vd_name, "akey", strlen("akey"));
	daos_csum_set(&iod.vd_kcsum, NULL, 0);
	iod.vd_nr	= 1;
	recx.rx_rsize	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= sizeof(buf);
	iod.vd_recxs	= &recx;
	iod.vd_eprs	= NULL;
	iod.vd_csums	= NULL;

	/** update record */
	print_message("Updating records with read-only container handle ...\n");
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, -DER_NO_PERM);
	print_message("got -DER_NO_PERM as expected\n");

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	/** close container handle */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("all is fine\n");

	MPI_Barrier(MPI_COMM_WORLD);
}


static const struct CMUnitTest capa_tests[] = {
	{ "CAPA1: query pool with invalid pool handle",
	  query, NULL, NULL},
	{ "CAPA2: create container with invalid pool handle",
	  create, NULL, NULL},
	{ "CAPA3: destroy container with invalid pool handle",
	  destroy, NULL, NULL},
	{ "CAPA4: open container with invalid pool handle",
	  open, NULL, NULL},
	{ "CAPA5: update/fetch with invalid pool handle",
	  io_invalid_poh, NULL, NULL},
	{ "CAPA6: update/fetch with invalid container handle",
	  io_invalid_coh, NULL, NULL},
	{ "CAPA7: update with read-only container handle",
	  update_ro, NULL, NULL},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_CONT_CREATE, true);
}

int
run_daos_capa_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS capability tests", capa_tests,
					 setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}