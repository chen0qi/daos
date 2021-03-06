/**
 * (C) Copyright 2018 Intel Corporation.
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
 * This file is part of daos, to test epoch IO.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

/* the temporary IO dir */
char test_io_dir[PATH_MAX] = { 0 };
/* the temporary IO working dir, will be cleanup for every running */
char test_io_work_dir[PATH_MAX] = { 0 };
/* the temporary IO fail dir, used to store the failed IO conf files */
char test_io_fail_dir[PATH_MAX] = { 0 };

/* the IO conf file */
const char *test_io_conf;

#define CMD_LINE_LEN_MAX	(1024)
#define CMD_LINE_ARGC_MAX	(16)
#define CMD_LINE_DBG		0

/*
 * To add predefined io_conf:
 * and add file name to predefined_io_confs array before the NULL.
 */
static char *predefined_io_confs[] = {
	"./io_conf/daos_io_conf_1",
	"./io_conf/daos_io_conf_2",
	NULL
};

static daos_size_t
test_recx_size(daos_recx_t *recxs, int recx_num, daos_size_t iod_size)
{
	daos_size_t	size = 0;
	int		i;

	if (recxs == NULL)
		return iod_size;

	for (i = 0; i < recx_num; i++)
		size += recxs[i].rx_nr;

	size *= iod_size;
	return size;
}

static int
epoch_io_mkdir(char *path)
{
	int	rc;

	rc = test_mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (rc)
		print_message("test_make_dirs %s failed, rc %d.\n", path, rc);

	return rc;
}

static void
test_buf_init(char *buf, daos_size_t size, daos_epoch_t epoch)
{
	memset(buf, 'A' + epoch, size);
}

static int
daos_test_cb_uf(test_arg_t *arg, struct test_op_record *op, char **rbuf,
		daos_size_t *rbuf_size, bool update)
{
	struct epoch_io_args		*eio_arg = &arg->eio_args;
	struct test_key_record		*key_rec = op->or_key_rec;
	struct test_update_fetch_arg	*uf_arg = &op->uf_arg;
	const char			*dkey = key_rec->or_dkey;
	const char			*akey = key_rec->or_akey;
	daos_size_t			 iod_size = key_rec->or_iod_size;
	bool				 array = uf_arg->ua_array;
	daos_iod_type_t			 iod_type;
	daos_size_t			 buf_size;
	char				*buf = NULL;
	struct ioreq			 req;
	int				 rc = 0;

	if (array)
		D_ASSERT(uf_arg->ua_recxs != NULL && uf_arg->ua_recx_num >= 1);
	else
		D_ASSERT(uf_arg->ua_recxs == NULL);

	iod_type = array ? DAOS_IOD_ARRAY : DAOS_IOD_SINGLE;
	ioreq_init(&req, arg->coh, eio_arg->op_oid, iod_type, arg);
	buf_size = test_recx_size(uf_arg->ua_recxs, uf_arg->ua_recx_num,
				  iod_size);
	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	if (update)
		test_buf_init(buf, buf_size, op->or_epoch);

	if (array) {
		if (update)
			insert_recxs(dkey, akey, iod_size, op->or_epoch,
				     uf_arg->ua_recxs, uf_arg->ua_recx_num, buf,
				     buf_size, &req);
		else
			lookup_recxs(dkey, akey, iod_size, op->or_epoch,
				     uf_arg->ua_recxs, uf_arg->ua_recx_num, buf,
				     buf_size, &req);
	} else {
		if (update)
			insert_single(dkey, akey, 0, buf, buf_size,
				      op->or_epoch, &req);
		else
			lookup_single(dkey, akey, 0, buf, buf_size,
				      op->or_epoch, &req);
	}

out:
	ioreq_fini(&req);
	if (update) {
		if (buf != NULL)
			D_FREE(buf);
	} else {
		if (rc == 0) {
			*rbuf = buf;
			*rbuf_size = buf_size;
		}
	}
	return rc;
}

static int
daos_test_cb_update(test_arg_t *arg, struct test_op_record *op,
		    char **rbuf, daos_size_t *rbuf_size)
{
	return daos_test_cb_uf(arg, op, rbuf, rbuf_size, true);
}

static int
vos_test_cb_update(test_arg_t *arg, struct test_op_record *op,
		   char **rbuf, daos_size_t *rbuf_size)
{
	return -DER_NOSYS;
}

static int
fio_test_cb_uf(test_arg_t *arg, struct test_op_record *op, char **rbuf,
	       daos_size_t *rbuf_size, bool update)
{
	struct test_key_record		*key_rec = op->or_key_rec;
	const char			*dkey = key_rec->or_dkey;
	const char			*akey = key_rec->or_akey;
	struct test_update_fetch_arg	*uf_arg = &op->uf_arg;
	daos_size_t			 iod_size = key_rec->or_iod_size;
	bool				 array = uf_arg->ua_array;
	daos_size_t			 buf_size;
	char				*buf = NULL, *data;
	int				 fd;
	off_t				 off;
	size_t				 len, data_len, total_len;
	int				 i;
	int				 rc = 0;

	if (array)
		D_ASSERT(uf_arg->ua_recxs != NULL && uf_arg->ua_recx_num >= 1);
	else
		D_ASSERT(uf_arg->ua_recxs == NULL);

	buf_size = test_recx_size(uf_arg->ua_recxs, uf_arg->ua_recx_num,
				  iod_size);
	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	if (update)
		test_buf_init(buf, buf_size, op->or_epoch);
	fd = array ? key_rec->or_fd_array : key_rec->or_fd_single;
	D_ASSERT(fd != 0);

	total_len = 0;
	if (array) {
		data = buf;
		for (i = 0; i < uf_arg->ua_recx_num; i++) {
			off = uf_arg->ua_recxs[i].rx_idx * iod_size;
			len = uf_arg->ua_recxs[i].rx_nr * iod_size;
			if (update)
				data_len = pwrite(fd, data, len, off);
			else
				data_len = pread(fd, data, len, off);
			if (data_len != len) {
				print_message("fio %s/%s failed, len %zu "
					      "got %zu.\n", dkey, akey,
					      len, data_len);
				D_GOTO(out, rc = -DER_IO);
			}
			data += len;
			total_len += len;
		}
	} else {
		if (update)
			total_len = pwrite(fd, buf, buf_size, 0);
		else
			total_len = pread(fd, buf, buf_size, 0);
	}
	if (total_len != buf_size) {
		print_message("fio %s/%s failed, buf_size "DF_U64", total_len "
			      "%zu.\n", dkey, akey, buf_size, total_len);
		rc = -DER_IO;
	}


out:
	if (update) {
		if (buf != NULL)
			D_FREE(buf);
	} else {
		if (rc == 0) {
			*rbuf = buf;
			*rbuf_size = buf_size;
		}
	}
	return rc;
}

static int
fio_test_cb_update(test_arg_t *arg, struct test_op_record *op,
		   char **rbuf, daos_size_t *rbuf_size)
{
	return fio_test_cb_uf(arg, op, rbuf, rbuf_size, true);
}

static int
daos_test_cb_fetch(test_arg_t *arg, struct test_op_record *op,
		   char **rbuf, daos_size_t *rbuf_size)
{
	return daos_test_cb_uf(arg, op, rbuf, rbuf_size, false);
}

static int
vos_test_cb_fetch(test_arg_t *arg, struct test_op_record *op,
		  char **rbuf, daos_size_t *rbuf_size)
{
	return -DER_NOSYS;
}

static int
fio_test_cb_fetch(test_arg_t *arg, struct test_op_record *op,
		  char **rbuf, daos_size_t *rbuf_size)
{
	return fio_test_cb_uf(arg, op, rbuf, rbuf_size, false);
}


static int
test_cb_noop(test_arg_t *arg, struct test_op_record *op,
	     char **rbuf, daos_size_t *rbuf_size)
{
	return -DER_NOSYS;
}

struct test_op_dict op_dict[] = {
	{
		.op_type	= TEST_OP_UPDATE,
		.op_str		= "update",
		.op_cb		= {
			daos_test_cb_update,
			vos_test_cb_update,
			fio_test_cb_update,
		},
	}, {
		.op_type	= TEST_OP_PUNCH,
		.op_str		= "punch",
		.op_cb		= {
			test_cb_noop,
			test_cb_noop,
			test_cb_noop,
		},
	}, {
		.op_type	= TEST_OP_EPOCH_DISCARD,
		.op_str		= "epoch_discard",
		.op_cb		= {
			test_cb_noop,
			test_cb_noop,
			test_cb_noop,
		},
	}, {
		.op_type	= TEST_OP_FETCH,
		.op_str		= "fetch",
		.op_cb		= {
			daos_test_cb_fetch,
			vos_test_cb_fetch,
			fio_test_cb_fetch,
		},
	}, {
		.op_type	= TEST_OP_ENUMERATE,
		.op_str		= "enumerate",
		.op_cb		= {
			test_cb_noop,
			test_cb_noop,
			test_cb_noop,
		},
	}, {
		.op_str		= NULL,
	}
};

static void
squeeze_spaces(char *line)
{
	char	*current = line;
	int	 spacing = 0;
	int	 leading_space = 1;

	for (; line && *line != '\n'; line++) {
		if (isspace(*line)) {
			if (!spacing && !leading_space) {
				*current++ = *line;
				spacing = 1;
			}
		} else {
			*current++ = *line;
			spacing = 0;
			leading_space = 0;
		}
	}
	*current = '\0';
}

static int
cmd_line_get(FILE *fp, char *line)
{
	char	*p;

	D_ASSERT(line != NULL && fp != NULL);
	do {
		if (fgets(line, CMD_LINE_LEN_MAX, fp) == NULL)
			return -DER_ENOENT;
		for (p = line; isspace(*p); p++)
			;
		if (*p != '\0' && *p != '#' && *p != '\n')
			break;
	} while (1);

	squeeze_spaces(line);

	return 0;
}

struct epoch_io_cmd_option {
	char	*opt_name;
	bool	 with_arg;
	char	 opt;
};

/* getopt_long with bug when calling it multiple times, so write this simple
 * helper function for parameter parsing, with similar behavior as getopt_long.
 */
static int	 eio_optind = 1;
static char	*eio_optarg;

int
epoch_io_getopt(int argc, char **argv, struct epoch_io_cmd_option options[])
{
	int	idx = eio_optind;
	char	*p;
	int	i;

	if (idx >= argc)
		return -1;

	p = argv[idx];
	eio_optind++;
	for (i = 0; options[i].opt_name != NULL; i++) {
		if ((strcmp(options[i].opt_name, argv[idx]) == 0) ||
		    (strlen(p) == 2 && *p == '-' &&
		     *(p + 1) == options[i].opt)) {
			if (options[i].with_arg) {
				if (eio_optind >= argc)
					return -1;
				eio_optind++;
				eio_optarg = argv[idx + 1];
			} else {
				eio_optarg = NULL;
			}
			return options[i].opt;
		}
	}
	return '?';
}

static int
recx_parse(char *recx_str, daos_recx_t **recxs, unsigned int *recx_num)
{
	daos_recx_t	*recx_allocated = NULL;
	char		 str[CMD_LINE_LEN_MAX] = { 0 };
	char		*p = str, *tmp;
	bool		 brace_unmatch = false;
	uint64_t	 rx_end;
	unsigned int	 idx = 0;

	D_ALLOC(recx_allocated, sizeof(daos_recx_t) * IOREQ_IOD_NR);
	if (recx_allocated == NULL)
		return -DER_NOMEM;

	strncpy(str, recx_str, CMD_LINE_LEN_MAX);
	while (*p != '\0') {
		p = strchr(p, '[');
		if (p == NULL)
			break;
		brace_unmatch = true;
		p++;
		while (*p == ' ')
			p++;
		tmp = strchr(p, ',');
		if (tmp == NULL)
			break;
		*tmp = '\0';
		tmp++;
		recx_allocated[idx].rx_idx = atol(p);
		p = tmp;
		while (*p == ' ')
			p++;
		tmp = strchr(p, ']');
		if (tmp == NULL) {
			print_message("no matching ] for %s.\n", p);
			break;
		}
		*tmp = '\0';
		rx_end = atol(p);
		if (rx_end <= recx_allocated[idx].rx_idx) {
			print_message("rx_end "DF_U64" <= rx_idx "DF_U64"\n",
				      rx_end, recx_allocated[idx].rx_idx);
			break;
		}
		brace_unmatch = false;
		recx_allocated[idx].rx_nr = rx_end - recx_allocated[idx].rx_idx;
		idx++;
		p = tmp + 1;
	}


	if (idx == 0 || brace_unmatch) {
		print_message("bad recx_str %s\n", p);
		if (recx_allocated)
			D_FREE(recx_allocated);
		return -DER_INVAL;
	}

	*recxs = recx_allocated;
	*recx_num = idx;
	return 0;
}

static struct test_key_record *
test_key_rec_lookup(test_arg_t *arg, char *dkey, char *akey)
{
	struct epoch_io_args	*eio_arg = &arg->eio_args;
	struct test_key_record	*key_rec;

	if (dkey == NULL)
		dkey = eio_arg->op_dkey;
	if (akey == NULL)
		akey = eio_arg->op_akey;
	if (dkey == NULL || akey == NULL)
		return NULL;

	d_list_for_each_entry(key_rec, &eio_arg->op_list, or_list) {
		if (strcmp(key_rec->or_dkey, dkey) == 0 &&
		    strcmp(key_rec->or_akey, akey) == 0)
			return key_rec;
	}

	D_ALLOC_PTR(key_rec);
	if (key_rec == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&key_rec->or_list);
	D_INIT_LIST_HEAD(&key_rec->or_queue);
	D_STRNDUP(key_rec->or_dkey, dkey, strlen(dkey));
	D_STRNDUP(key_rec->or_akey, akey, strlen(akey));
	key_rec->or_iod_size = eio_arg->op_iod_size;
	key_rec->or_replayed_epoch = 0;
	key_rec->or_fd_array = 0;
	key_rec->or_fd_single = 0;
	key_rec->or_op_num = 0;
	d_list_add_tail(&key_rec->or_list, &eio_arg->op_list);

	return key_rec;
}

static void
test_op_rec_free(struct test_op_record *op_rec)
{
	/* the fetch OP is not in queue */
	if (!d_list_empty(&op_rec->or_queue_link)) {
		D_ASSERT(op_rec->or_key_rec != NULL);
		d_list_del_init(&op_rec->or_queue_link);
		op_rec->or_key_rec->or_op_num--;
	}
	op_rec->or_key_rec = NULL;

	switch (op_rec->or_op) {
	case TEST_OP_UPDATE:
	case TEST_OP_FETCH:
		if (op_rec->uf_arg.ua_recxs)
			D_FREE(op_rec->uf_arg.ua_recxs);
		break;
	default:
		break;
	}

	D_FREE(op_rec);
}

static void
test_key_rec_free(struct test_key_record *key_rec)
{
	struct test_op_record	*op_rec, *op_rec_tmp;

	d_list_for_each_entry_safe(op_rec, op_rec_tmp, &key_rec->or_queue,
				   or_queue_link) {
		test_op_rec_free(op_rec);
	}
	d_list_del_init(&key_rec->or_list);
	if (key_rec->or_dkey)
		D_FREE(key_rec->or_dkey);
	if (key_rec->or_akey)
		D_FREE(key_rec->or_akey);
	if (key_rec->or_fd_array) {
		close(key_rec->or_fd_array);
		key_rec->or_fd_array = 0;
	}
	if (key_rec->or_fd_single) {
		close(key_rec->or_fd_single);
		key_rec->or_fd_single = 0;
	}
	D_FREE(key_rec);
}

static void
test_eio_arg_oplist_free(test_arg_t	*arg)
{
	struct epoch_io_args	*eio_arg = &arg->eio_args;
	struct test_key_record	*key_rec, *key_rec_tmp;

	d_list_for_each_entry_safe(key_rec, key_rec_tmp, &eio_arg->op_list,
				   or_list) {
		test_key_rec_free(key_rec);
	}
}

static void
test_key_rec_add_op(struct test_key_record *key_rec,
		    struct test_op_record *op_rec)
{
	struct test_op_record	*rec = NULL;

	op_rec->or_key_rec = key_rec;
	/* insert modification OP to the queue in epoch order */
	if (test_op_is_modify(op_rec->or_op)) {
		d_list_for_each_entry(rec, &key_rec->or_queue, or_queue_link) {
			if (rec->or_epoch > op_rec->or_epoch)
				break;
		}
		d_list_add_tail(&op_rec->or_queue_link, &rec->or_queue_link);
		key_rec->or_op_num++;
#if CMD_LINE_DBG
		print_message("added op %d, epoch "DF_U64", dkey %s akey %s, "
			      "to queue, op_num %d.\n", op_rec->or_op,
			      op_rec->or_epoch, key_rec->or_dkey,
			      key_rec->or_akey, key_rec->or_op_num);

#endif
	}
}

static int
test_op_record_bind(test_arg_t *arg, char *dkey, char *akey,
		    struct test_op_record *op_rec)
{
	struct epoch_io_args	*eio_arg = &arg->eio_args;
	struct test_key_record	*key_rec;

	key_rec = test_key_rec_lookup(arg, dkey, akey);
	if (key_rec == NULL) {
		print_message("test_key_rec_lookup (dkey %s akey %s) failed "
			       "possibly because dkey/akey not set.\n",
			       dkey, akey);
		return -DER_INVAL;
	}

	if (key_rec->or_iod_size != eio_arg->op_iod_size) {
		print_message("cannot set different iod_size for same "
			       "dkey/akey ("DF_U64", "DF_U64").\n",
			       key_rec->or_iod_size, eio_arg->op_iod_size);
		return -DER_INVAL;
	}

	test_key_rec_add_op(key_rec, op_rec);
	return 0;
}

static int
cmd_parse_update_fetch(test_arg_t *arg, int argc, char **argv, bool update,
		       struct test_op_record **op)
{
	struct test_op_record		*op_rec;
	struct test_update_fetch_arg	*uf_arg;
	daos_recx_t			*recxs = NULL;
	unsigned int			 recx_num = 0;
	char				*dkey = NULL;
	char				*akey = NULL;
	daos_epoch_t			 epoch = 1;
	bool				 array = true;
	int				 opt;
	int				 rc = 0;

	static struct epoch_io_cmd_option options[] = {
		{"--dkey",	true,	'd'},
		{"--akey",	true,	'a'},
		{"--single",	false,	's'},
		{"--epoch",	true,	'e'},
		{"--recx",	true,	'r'},
		{0}
	};

	D_ALLOC_PTR(op_rec);
	if (op_rec == NULL)
		return -DER_NOMEM;
	D_INIT_LIST_HEAD(&op_rec->or_queue_link);
	uf_arg = &op_rec->uf_arg;

	eio_optind = 1;
	while ((opt = epoch_io_getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'e':
			epoch = atoi(eio_optarg);
			break;
		case 'd':
			D_STRNDUP(dkey, eio_optarg, strlen(eio_optarg));
			break;
		case 'a':
			D_STRNDUP(akey, eio_optarg, strlen(eio_optarg));
			break;
		case 's':
			array = false;
			break;
		case 'r':
			rc = recx_parse(eio_optarg, &recxs, &recx_num);
			if (rc) {
				print_message("parse recxs %s failed, rc %d.\n",
					      eio_optarg, rc);
				D_GOTO(out, rc);
			}
			uf_arg->ua_recxs = recxs;
			uf_arg->ua_recx_num = recx_num;
#if CMD_LINE_DBG
			int i;

			for (i = 0; i < recx_num; i++) {
				print_message("parsed recx - rx_idx[%d] "DF_U64
					      ", rx_nr[%d] "DF_U64"\n",
					      i, recxs[i].rx_idx,
					      i, recxs[i].rx_nr);
			}
#endif
			break;
		default:
			print_message("Unknown Option %c\n", opt);
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	op_rec->or_epoch = epoch;
	op_rec->or_op = update ? TEST_OP_UPDATE : TEST_OP_FETCH;
	uf_arg->ua_array = array;
	uf_arg->ua_update = update;
	if (uf_arg->ua_array && uf_arg->ua_recxs == NULL) {
		print_message("no recx specified for array update/fetch.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = test_op_record_bind(arg, dkey, akey, op_rec);
	if (rc == 0)
		*op = op_rec;
	else
		print_message("test_op_record_bind(dkey %s akey %s failed.\n",
			      dkey, akey);

out:
	if (dkey)
		D_FREE(dkey);
	if (akey)
		D_FREE(akey);
	if (rc && op_rec)
		test_op_rec_free(op_rec);
	return rc;
}

/* parse the cmd line to argc argv[] */
static int
cmd_parse_argv(char *cmd, int *argc, char *argv[])
{
	int	 idx = 0;
	char	*p = cmd;

	while (*p != '\0') {
		while (*p == ' ')
			p++;
		if (idx >= CMD_LINE_ARGC_MAX) {
			print_message("too many args.\n");
			return -DER_INVAL;
		}
		if (*p == '"') {
			p++;
			if (*p == '\0')
				return -DER_INVAL;
			argv[idx++] = p;
			p = strchr(p, '"');
			if (p == NULL)
				return -DER_INVAL;
			*p = '\0';
			p++;
		} else {
			if (*p == '\0')
				break;
			argv[idx++] = p;
			p = strchr(p, ' ');
			if (p == NULL)
				break;
			*p = '\0';
			p++;
		}
	}
	*argc = idx;

	return 0;
}

static int
cmd_line_parse(test_arg_t *arg, char *cmd_line, struct test_op_record **op)
{
	char			 cmd[CMD_LINE_LEN_MAX] = { 0 };
	struct test_op_record	*op_rec = NULL;
	char			*argv[CMD_LINE_ARGC_MAX] = { 0 };
	char			*dkey = NULL;
	char			*akey = NULL;
	int			 argc = 0;
	int			 rc = 0;

	strncpy(cmd, cmd_line, CMD_LINE_LEN_MAX);
#if CMD_LINE_DBG
	print_message("parsing cmd: %s.\n", cmd);
#endif

	rc = cmd_parse_argv(cmd, &argc, argv);
	if (rc != 0) {
		print_message("bad format %s.\n", cmd);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (argc < 2)
		D_GOTO(out, rc = -DER_INVAL);

	if (strcmp(argv[0], "test_lvl") == 0) {
		if (strcmp(argv[1], "daos") == 0) {
			arg->eio_args.op_lvl = TEST_LVL_DAOS;
		} else if (strcmp(argv[1], "vos") == 0) {
			arg->eio_args.op_lvl = TEST_LVL_VOS;
			print_message("vos level test not supported now.\n");
			rc = -DER_INVAL;
		} else {
			print_message("bad test_lvl %s.\n", argv[1]);
			rc = -DER_INVAL;
		}
	} else if (strcmp(argv[0], "dkey") == 0) {
		dkey = argv[1];
		if (arg->eio_args.op_dkey != NULL)
			D_FREE(arg->eio_args.op_dkey);
		D_STRNDUP(arg->eio_args.op_dkey, dkey, strlen(dkey));
	} else if (strcmp(argv[0], "akey") == 0) {
		akey = argv[1];
		if (arg->eio_args.op_akey != NULL)
			D_FREE(arg->eio_args.op_akey);
		D_STRNDUP(arg->eio_args.op_akey, akey, strlen(akey));
	} else if (strcmp(argv[0], "iod_size") == 0) {
		arg->eio_args.op_iod_size = atoi(argv[1]);
	} else if (strcmp(argv[0], "update") == 0) {
		rc = cmd_parse_update_fetch(arg, argc, argv, true, &op_rec);
	} else if (strcmp(argv[0], "fetch") == 0) {
		rc = cmd_parse_update_fetch(arg, argc, argv, false, &op_rec);
	} else {
		print_message("unknown cmd %s.\n", argv[0]);
		rc = -DER_INVAL;
	}

out:
	if (rc == 0)
		*op = op_rec;
	return rc;
}

/* replay the OPs which epoch <= /a epoch in key_rec's op_queue */
static int
test_op_queue_replay(test_arg_t *arg, struct test_key_record *key_rec,
		     daos_epoch_t epoch)
{
	struct test_op_record	*op_rec;
	char			 akey_dir[PATH_MAX] = { 0 };
	char			 array_path[PATH_MAX] = { 0 };
	char			 single_path[PATH_MAX] = { 0 };
	int			 rc = 0;

#if CMD_LINE_DBG
	print_message("replay %s/%s, epoch "DF_U64", replayed_epoch "DF_U64"\n",
		      key_rec->or_dkey, key_rec->or_akey, epoch,
		      key_rec->or_replayed_epoch);
#endif
	/* replay from beginning if read epoch behind replayed epoch,
	 * so verify from low epoch to high epoch will be faster.
	 */
	if (epoch < key_rec->or_replayed_epoch) {
		D_ASSERT(key_rec->or_fd_array != 0);
		close(key_rec->or_fd_array);
		key_rec->or_fd_array = 0;

		D_ASSERT(key_rec->or_fd_single != 0);
		close(key_rec->or_fd_single);
		key_rec->or_fd_single = 0;

		key_rec->or_replayed_epoch = 0;
	}

	if (key_rec->or_replayed_epoch == 0) {
		snprintf(akey_dir, PATH_MAX, "%s/%s/%s", test_io_work_dir,
			 key_rec->or_dkey, key_rec->or_akey);
		test_rmdir(akey_dir, true);
		rc = epoch_io_mkdir(akey_dir);
		if (rc) {
			print_message("failed to mkdir %s, rc %d.\n",
				      akey_dir, rc);
			return rc;
		}
		snprintf(array_path, PATH_MAX, "%s/array", akey_dir);
		snprintf(single_path, PATH_MAX, "%s/single", akey_dir);
		key_rec->or_fd_array = open(array_path,
			O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (key_rec->or_fd_array == 0) {
			print_message("failed to open %s, %d(%s)\n",
				      array_path, errno, strerror(errno));
			return daos_errno2der(errno);
		}
		key_rec->or_fd_single = open(single_path,
			O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (key_rec->or_fd_single == 0) {
			print_message("failed to open %s, %d(%s)\n",
				      single_path, errno, strerror(errno));
			close(key_rec->or_fd_array);
			key_rec->or_fd_array = 0;
			return daos_errno2der(errno);
		}
	}

	d_list_for_each_entry(op_rec, &key_rec->or_queue, or_queue_link) {
		if (op_rec->or_epoch < key_rec->or_replayed_epoch)
			continue;
		if (op_rec->or_epoch > epoch)
			break;
		rc = op_dict[op_rec->or_op].op_cb[TEST_LVL_FIO](
			arg, op_rec, NULL, NULL);
		if (rc == 0) {
			key_rec->or_replayed_epoch = op_rec->or_epoch;
		} else {
			print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n",
				      op_rec->or_op, TEST_LVL_FIO, rc);
			close(key_rec->or_fd_array);
			key_rec->or_fd_array = 0;
			close(key_rec->or_fd_single);
			key_rec->or_fd_single = 0;
			key_rec->or_replayed_epoch = 0;
			return rc;
		}
	}

	return rc;
}

static int
cmd_line_run(test_arg_t *arg, struct test_op_record *op_rec)
{
	int		 op = op_rec->or_op;
	int		 lvl = arg->eio_args.op_lvl;
	char		*buf = NULL, *f_buf = NULL;
	daos_size_t	 size = 0, f_size = 0;
	int		 rc = 0;

	D_ASSERT(op >= TEST_OP_MIN && op <= TEST_OP_MAX);
	D_ASSERT(lvl == TEST_LVL_DAOS || lvl == TEST_LVL_VOS);

	/* for modification OP, just go through DAOS stack and return */
	if (test_op_is_modify(op))
		return op_dict[op].op_cb[lvl](arg, op_rec, NULL, 0);

	/* for verification OP, firstly retrieve it through DAOS stack */
	rc = op_dict[op].op_cb[lvl](arg, op_rec, &buf, &size);
	if (rc) {
		print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n",
			      op, lvl, rc);
		D_GOTO(out, rc);
	}

	/* then replay the modification OPs in the queue, retrieve it through
	 * fio and compare the result data.
	 */
	rc = test_op_queue_replay(arg, op_rec->or_key_rec, op_rec->or_epoch);
	if (rc) {
		print_message("test_op_queue_replay epoch "DF_U64" failed, "
			      "rc %d.\n", op_rec->or_epoch, rc);
		D_GOTO(out, rc);
	}

	rc = op_dict[op].op_cb[TEST_LVL_FIO](arg, op_rec, &f_buf,  &f_size);
	if (rc) {
		print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n",
			      op, TEST_LVL_FIO, rc);
		D_GOTO(out, rc);
	}

	if (size != f_size) {
		print_message("size mismatch ("DF_U64" vs "DF_U64").\n",
			      size, f_size);
		D_GOTO(out, rc = -DER_MISMATCH);
	}
	if (memcmp(buf, f_buf, size) != 0) {
		int	i, j;

		print_message("data verification failed.\n");
		/* print first 8 mismatched data */
		for (i = 0, j = 0; i < size && j < 8; i++) {
			if (buf[i] != f_buf[i]) {
				print_message("offset %d expect %d, got %d.\n",
					      i, f_buf[i], buf[i]);
				j++;
			}
		}
		rc = -DER_MISMATCH;
	}

out:
	if (buf)
		D_FREE(buf);
	if (f_buf)
		D_FREE(f_buf);
	return rc;
}

int
io_conf_run(test_arg_t *arg, const char *io_conf)
{
	struct test_op_record	*op = NULL;
	FILE			*fp;
	char			 cmd_line[CMD_LINE_LEN_MAX];
	int			 rc = 0;

	if (io_conf == NULL || strlen(io_conf) == 0) {
		print_message("invalid io_conf.\n");
		return -DER_INVAL;
	}
	if (arg->myrank != 0)
		return 0;

	fp = fopen(io_conf, "r");
	if (fp == NULL) {
		print_message("failed to open io_conf %s, %d(%s).\n",
			      io_conf, errno, strerror(errno));
		return daos_errno2der(errno);
	}

	do {
		memset(cmd_line, 0, CMD_LINE_LEN_MAX);
		if (cmd_line_get(fp, cmd_line) != 0)
			break;

		rc = cmd_line_parse(arg, cmd_line, &op);
		if (rc != 0) {
			print_message("bad cmd_line %s, exit.\n", cmd_line);
			break;
		}

		if (op != NULL) {
			rc = cmd_line_run(arg, op);
			if (rc) {
				print_message("run cmd_line %s failed, "
					      "rc %d.\n", cmd_line, rc);
				break;
			}
		}
	} while (1);

	fclose(fp);
	return rc;
}

static void
epoch_io_predefined(void **state)
{
	test_arg_t	*arg = *state;
	int		 i;
	int		 rc;

	if (test_io_conf != NULL && strlen(test_io_conf) > 0) {
		print_message("will run predefined io_conf %s ...\n",
			      test_io_conf);
		rc = io_conf_run(arg, test_io_conf);
		if (rc)
			print_message("io_conf %s failed, rc %d.\n",
				      test_io_conf, rc);
		else
			print_message("io_conf %s succeed.\n", test_io_conf);
		assert_int_equal(rc, 0);
		return;
	}

	for (i = 0; predefined_io_confs[i] != NULL; i++) {
		print_message("will run predefined io_conf %s ...\n",
			      predefined_io_confs[i]);
		rc = io_conf_run(arg, predefined_io_confs[i]);
		if (rc)
			print_message("io_conf %s failed, rc %d.\n",
				      predefined_io_confs[i], rc);
		else
			print_message("io_conf %s succeed.\n",
				     predefined_io_confs[i]);
		assert_int_equal(rc, 0);
		test_eio_arg_oplist_free(arg);
	}
}

static const struct CMUnitTest epoch_io_tests[] = {
	{ "EPOCH_IO1: predefined IO conf testing",
	  epoch_io_predefined, async_disable, test_case_teardown},
};

static int
epoch_io_setup(void **state)
{
	test_arg_t		*arg;
	struct epoch_io_args	*eio_arg;
	char			 tmp_str[64] = { 0 };
	int			 rc;

	/* generate the temporary IO dir for epoch IO test */
	if (test_io_dir[0] == 0)
		strncpy(test_io_dir, "/tmp", 4);
	snprintf(tmp_str, 64, "/daos_epoch_io_test/%d/", geteuid());
	strncat(test_io_dir, tmp_str, 64);
	rc = epoch_io_mkdir(test_io_dir);
	if (rc)
		return rc;

	/* cleanup/re-create temporary IO working dir */
	snprintf(test_io_work_dir, PATH_MAX, "%swork/", test_io_dir);
	test_rmdir(test_io_work_dir, true);
	rc = epoch_io_mkdir(test_io_work_dir);
	if (rc)
		return rc;

	/* create IO fail dir */
	snprintf(test_io_fail_dir, PATH_MAX, "%sfail/", test_io_dir);
	rc = epoch_io_mkdir(test_io_fail_dir);
	if (rc)
		return rc;
	print_message("created test_io_dir %s, and subdirs %s, %s.\n",
		      test_io_dir, test_io_work_dir, test_io_fail_dir);

	obj_setup(state);
	arg = *state;
	eio_arg = &arg->eio_args;
	D_INIT_LIST_HEAD(&eio_arg->op_list);
	eio_arg->op_lvl = TEST_LVL_DAOS;
	eio_arg->op_iod_size = 1;
	eio_arg->op_oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);

	return 0;
}

static int
epoch_io_teardown(void **state)
{
	test_arg_t		*arg = *state;
	struct epoch_io_args	*eio_arg = &arg->eio_args;

	test_eio_arg_oplist_free(arg);

	if (eio_arg->op_dkey)
		D_FREE(eio_arg->op_dkey);
	if (eio_arg->op_akey)
		D_FREE(eio_arg->op_akey);

	return test_teardown(state);
}

int
run_daos_epoch_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS epoch I/O tests",
			epoch_io_tests, epoch_io_setup,
			epoch_io_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
