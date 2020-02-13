// SPDX-License-Identifier: GPL-2.0+
#include "config.h"
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <getopt.h>
#include "drv/hisi_qm_udrv.h"
#include "smm.h"
#include "test_lib.h"
#ifndef WD_SCHED
#include "wd.h"
#endif

#define ZLIB_HEADER "\x78\x9c"
#define ZLIB_HEADER_SZ 2

/*
 * We use a extra field for gzip block length. So the fourth byte is \x04.
 * This is necessary because our software don't know the size of block when
 * using an hardware decompresser (It is known by hardware). This help our
 * decompresser to work and helpfully, compatible with gzip.
 */
#define GZIP_HEADER "\x1f\x8b\x08\x04\x00\x00\x00\x00\x00\x03"
#define GZIP_HEADER_SZ 10
#define GZIP_EXTRA_SZ 10
#define GZIP_TAIL_SZ 8

/* bytes of data for a request */
static int block_size = 512000;
static int req_cache_num = 4;
static int q_num = 1;

static struct hizip_priv {
	int alg_type;
	int op_type;
	int dw9;
	int total_len;
	struct hisi_zip_sqe *msgs;
	FILE *sfile, *dfile;

#ifndef WD_SCHED
	struct wd_queue	*qs;
	int		q_num;
	int		q_send_idx;
	int		q_recv_idx;
	struct wd_msg	*caches;
	int		cache_num;
	int		cache_size;
	int		avail_cache;
	int		c_send_idx;
	int		c_recv_idx;
	void		*ss_region;
#endif
} hizip_priv;

#ifdef WD_SCHED
static struct wd_scheduler sched = {
	.priv = &hizip_priv,
};

static void hizip_wd_sched_init_cache(struct wd_scheduler *sched, int i,
				      void *priv)
{
	struct wd_msg *wd_msg = &sched->msgs[i];
	struct hisi_zip_sqe *msg;
	struct hizip_priv *ctx = priv;
	void *data_in, *data_out;

	msg = wd_msg->msg = &ctx->msgs[i];
	msg->dw9 = ctx->dw9;
	msg->dest_avail_out = sched->msg_data_size;

	if (sched->qs[0].dev_flags & UACCE_DEV_SVA) {
		data_in = wd_msg->data_in;
		data_out = wd_msg->data_out;
	} else {
		data_in = wd_get_pa_from_va(&sched->qs[0], wd_msg->data_in);
		data_out = wd_get_pa_from_va(&sched->qs[0], wd_msg->data_out);
	}
	msg->source_addr_l = (__u64)data_in & 0xffffffff;
	msg->source_addr_h = (__u64)data_in >> 32;
	msg->dest_addr_l = (__u64)data_out & 0xffffffff;
	msg->dest_addr_h = (__u64)data_out >> 32;

	dbg("init sched cache %d: %p, %p\n", i, wd_msg, msg);
}

static int hizip_wd_sched_input(struct wd_msg *msg, void *priv)
{
	size_t ilen, templen, real_len, sz;
	struct hisi_zip_sqe *m = msg->msg;

	ilen = hizip_priv.total_len > block_size ?
		block_size : hizip_priv.total_len;
	templen = ilen;
	hizip_priv.total_len -= ilen;
	if (hizip_priv.op_type == INFLATE) {
		if (hizip_priv.alg_type == ZLIB) {
			sz = fread(msg->data_in, 1, ZLIB_HEADER_SZ,
				   hizip_priv.sfile);
			SYS_ERR_COND(sz != ZLIB_HEADER_SZ, "read");
			ilen -= ZLIB_HEADER_SZ;
		} else {
			sz = fread(msg->data_in, 1, GZIP_HEADER_SZ,
				   hizip_priv.sfile);
			SYS_ERR_COND(sz != GZIP_HEADER_SZ, "read");
			ilen -= GZIP_HEADER_SZ;
			if (*((char *)msg->data_in + 3) == 0x04) {
				sz = fread(msg->data_in, 1, GZIP_EXTRA_SZ,
					   hizip_priv.sfile);
				memcpy(&ilen, msg->data_in + 6, 4);
				dbg("gzip iuput len %ld\n", ilen);
				SYS_ERR_COND(ilen > block_size * 2,
				    "gzip protocol_len(%ld) > dmabuf_size(%d)\n",
				    ilen, block_size);
				real_len = GZIP_HEADER_SZ
					+ GZIP_EXTRA_SZ + ilen;
				hizip_priv.total_len = hizip_priv.total_len
					+ templen - real_len;
			}
		}
	}

	sz = fread(msg->data_in, 1, ilen, hizip_priv.sfile);
	SYS_ERR_COND(sz != ilen, "read");

	m->input_data_length = ilen;

	dbg("zip input(%p, %p): %x, %x, %x, %x, %d, %d\n",
	    msg, m,
	    m->source_addr_l, m->source_addr_h,
	    m->dest_addr_l, m->dest_addr_h,
	    m->dest_avail_out, m->input_data_length);

	return 0;
}

static int hizip_wd_sched_output(struct wd_msg *msg, void *priv)
{
	size_t sz;
	struct hisi_zip_sqe *m = msg->msg;
	__u32 status = m->dw3 & 0xff;
	__u32 type = m->dw9 & 0xff;
	char gzip_extra[GZIP_EXTRA_SZ] = {0x08, 0x00, 0x48, 0x69, 0x04, 0x00,
					  0x00, 0x00, 0x00, 0x00};

	dbg("zip output(%p, %p): %x, %x, %x, %x, %d, %d, consume=%d, out=%d\n",
	    msg, m,
	    m->source_addr_l, m->source_addr_h,
	    m->dest_addr_l, m->dest_addr_h,
	    m->dest_avail_out, m->input_data_length, m->consumed, m->produced);

	SYS_ERR_COND(status != 0 && status != 0x0d, "bad status (s=%d, t=%d)\n",
		     status, type);
	if (hizip_priv.op_type == DEFLATE) {

		if (hizip_priv.alg_type == ZLIB) {
			sz = fwrite(ZLIB_HEADER, 1, ZLIB_HEADER_SZ,
				    hizip_priv.dfile);
			SYS_ERR_COND(sz != ZLIB_HEADER_SZ, "write");
		} else {
			sz = fwrite(GZIP_HEADER, 1, GZIP_HEADER_SZ,
				    hizip_priv.dfile);
			SYS_ERR_COND(sz != GZIP_HEADER_SZ, "write");
			memcpy(gzip_extra + 6, &m->produced, 4);
			sz = fwrite(gzip_extra, 1, GZIP_EXTRA_SZ,
				    hizip_priv.dfile);
			SYS_ERR_COND(sz != GZIP_EXTRA_SZ, "write");
		}
	}
	sz = fwrite(msg->data_out, 1, m->produced, hizip_priv.dfile);
	SYS_ERR_COND(sz != m->produced, "write");
	return 0;
}

int hizip_init(int alg_type, int op_type)
{
	int ret = -ENOMEM, i;
	char *alg;
	struct hisi_qm_priv *priv;

	sched.q_num = q_num;
	sched.ss_region_size = 0; /* let system make decision */
	sched.msg_cache_num = req_cache_num;
	sched.msg_data_size = block_size * 2; /* use twice size of the input
						 data, hope it is engouth for
						 output */
	sched.init_cache = hizip_wd_sched_init_cache;
	sched.input = hizip_wd_sched_input;
	sched.output = hizip_wd_sched_output;

	sched.qs = calloc(q_num, sizeof(*sched.qs));
	if (!sched.qs)
		return -ENOMEM;

	hizip_priv.msgs = calloc(req_cache_num, sizeof(*hizip_priv.msgs));
	if (!hizip_priv.msgs)
		goto err_with_qs;


	hizip_priv.alg_type = alg_type;
	hizip_priv.op_type = op_type;
	if (alg_type == ZLIB) {
		alg = "zlib";
		hizip_priv.dw9 = 2;
	} else {
		alg = "gzip";
		hizip_priv.dw9 = 3;
	}

	for (i = 0; i < q_num; i++) {
		sched.qs[i].capa.alg = alg;
		priv = (struct hisi_qm_priv *)sched.qs[i].capa.priv;
		priv->sqe_size = sizeof(struct hisi_zip_sqe);
		priv->op_type = hizip_priv.op_type;
	}
	ret = wd_sched_init(&sched);
	if (ret)
		goto err_with_msgs;

	return 0;

err_with_msgs:
	free(hizip_priv.msgs);
err_with_qs:
	free(sched.qs);
	return ret;
}

void hizip_fini(void)
{
	wd_sched_fini(&sched);
	free(hizip_priv.msgs);
	free(sched.qs);
}

void hizip_deflate(FILE *source, FILE *dest)
{
	int fd;
	struct stat s;
	int ret;

	fd = fileno(source);
	SYS_ERR_COND(fstat(fd, &s) < 0, "fstat");
	hizip_priv.total_len = s.st_size;
	SYS_ERR_COND(!hizip_priv.total_len, "input file length zero");
	hizip_priv.sfile = source;
	hizip_priv.dfile = dest;

	/* ZLIB engine can do only one time with buffer less than 16M */
	if (hizip_priv.alg_type == ZLIB) {
		SYS_ERR_COND(hizip_priv.total_len > block_size,
			     "zip total_len(%d) > block_size(%d)\n",
			     hizip_priv.total_len, block_size);
		SYS_ERR_COND(block_size > 16 * 1024 * 1024,
			     "block_size (%d) > 16MB hw limit!\n",
			     hizip_priv.total_len);
	}

	while (hizip_priv.total_len || !wd_sched_empty(&sched)) {
		dbg("request loop: total_len=%d\n", hizip_priv.total_len);
		ret = wd_sched_work(&sched, hizip_priv.total_len);
		SYS_ERR_COND(ret < 0, "wd_sched_work");
	}

	fclose(dest);
}

void hizip_def(FILE *source, FILE *dest, int alg_type, int op_type)
{
	int ret;

	ret = hizip_init(alg_type, op_type);
	SYS_ERR_COND(ret, "hizip init fail\n");

	hizip_deflate(stdin, stdout);

	hizip_fini();
}
#else

struct wd_msg {
	void *data_in;
	void *data_out;
	void *msg;	/* the hw share buffer itself */
};

int hizip_init(int alg_type, int op_type)
{
	int ret = -ENOMEM, i;
	char *alg;
	struct hisi_qm_priv *qm_priv;
	int ss_region_size;
	struct hizip_priv *priv;
	void *data_in, *data_out;

	priv = &hizip_priv;

	priv->cache_num	= 4;
	priv->cache_size = block_size;
	priv->c_send_idx = 0;
	priv->c_recv_idx = 0;
	priv->avail_cache = priv->cache_num;
	priv->caches = calloc(priv->cache_num, sizeof(struct wd_msg));
	if (!priv->caches)
		return ret;

	priv->msgs = calloc(priv->cache_num, sizeof(struct hisi_zip_sqe));
	if (!hizip_priv.msgs)
		return ret;

	priv->alg_type = alg_type;
	priv->op_type = op_type;
	if (alg_type == ZLIB) {
		alg = "zlib";
		priv->dw9 = 2;
	} else {
		alg = "gzip";
		priv->dw9 = 3;
	}

	priv->q_num = q_num;
	priv->q_send_idx = 0;
	priv->q_recv_idx = 0;
	priv->qs = calloc(q_num, sizeof(struct wd_queue));
	if (priv->qs == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < priv->q_num; i++) {
		priv->qs[i].capa.alg = alg;
		qm_priv = (struct hisi_qm_priv *)priv->qs[i].capa.priv;
		qm_priv->sqe_size = sizeof(struct hisi_zip_sqe);
		qm_priv->op_type = hizip_priv.op_type;

		ret = wd_request_queue(&priv->qs[i]);
		if (ret) {
			fprintf(stderr, "Failed to request queue (%d).\n", ret);
			return ret;
		}
	}

	/* smm_init() costs two pages, and each smm_alloc() costs one additional
	 * page.
	 */
	ss_region_size = priv->cache_num * (block_size * 2 + 4096) * 2 + 4096 * 2;
	priv->ss_region = wd_reserve_memory(&hizip_priv.qs[0], ss_region_size);
	if (priv->ss_region == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = smm_init(priv->ss_region, ss_region_size, 0xF);
	if (ret)
		goto out;

	for (i = 0; i< priv->cache_num; i++) {
		priv->caches[i].data_in = smm_alloc(priv->ss_region,
						    priv->cache_size);
		priv->caches[i].data_out = smm_alloc(priv->ss_region,
						     priv->cache_size);
		priv->caches[i].msg = &priv->msgs[i];
		data_in = wd_get_pa_from_va(&priv->qs[0],
					    priv->caches[i].data_in);
		data_out = wd_get_pa_from_va(&priv->qs[0],
					     priv->caches[i].data_out);
		priv->msgs[i].source_addr_l = (__u64)data_in & 0xffffffff;
		priv->msgs[i].source_addr_h = (__u64)data_in >> 32;
		priv->msgs[i].dest_addr_l = (__u64)data_out & 0xffffffff;
		priv->msgs[i].dest_addr_h = (__u64)data_out >> 32;
		priv->msgs[i].dw9 = priv->dw9;
		priv->msgs[i].dest_avail_out = priv->cache_size;
	}
	return 0;
out:
	for (i = 0; i < priv->q_num; i++)
		wd_release_queue(&priv->qs[i]);
	return ret;
}

void hizip_fini(struct hizip_priv *priv)
{
	/*
	 * TODO: fix this double free issue
	int i;
	for (i = 0; i < priv->q_num; i++)
		wd_release_queue(&priv->qs[i]);
	*/
	free(priv->caches);
	free(priv->msgs);
	free(priv->qs);
}

void hizip_read(struct hizip_priv *priv, int msg_idx, size_t ilen)
{
	size_t sz, temp_len, real_len;
	void *data_in;
	struct hisi_zip_sqe *msg;

	temp_len = ilen;
	data_in = priv->caches[msg_idx].data_in;
	msg = priv->caches[msg_idx].msg;
	msg->input_data_length = ilen;

	if (priv->op_type == INFLATE) {
		if (priv->alg_type == ZLIB) {
			sz = fread(data_in, 1, ZLIB_HEADER_SZ, priv->sfile);
			SYS_ERR_COND(sz != ZLIB_HEADER_SZ, "read zlib hd err");
			ilen -= ZLIB_HEADER_SZ;
		} else {
			sz = fread(data_in, 1, GZIP_HEADER_SZ, priv->sfile);
			SYS_ERR_COND(sz != GZIP_HEADER_SZ, "read gzip hd err");
			ilen -= GZIP_HEADER_SZ;
			if (*((char *)data_in + 3) == 0x04) {
				sz = fread(data_in, 1, GZIP_EXTRA_SZ,
					   priv->sfile);
				memcpy(&ilen, data_in + 6, 4);
				dbg("gzip input len %ld\n", ilen);
				SYS_ERR_COND(ilen > block_size * 2,
				   "gzip protocol_len(%ld) > dmabuf_size(%d)\n",
				   ilen, block_size);
				real_len = GZIP_HEADER_SZ + GZIP_EXTRA_SZ +
					   ilen;
				priv->total_len = priv->total_len +
					   temp_len - real_len;
			}
		}
	}
	sz = fread(data_in, 1, ilen, priv->sfile);
	SYS_ERR_COND(sz != ilen, "read data err");
}

void hizip_write(struct hizip_priv *priv, int msg_idx, size_t olen)
{
	size_t sz;
	unsigned int status, type;
	char gzip_extra[GZIP_EXTRA_SZ] = {0x08, 0x00, 0x48, 0x69, 0x04, 0x00,
					  0x00, 0x00, 0x00, 0x00};
	void *data_out;
	struct hisi_zip_sqe *msg = priv->caches[msg_idx].msg;

	status = msg->dw3 & 0xff;
	type = msg->dw9 & 0xff;
	data_out = priv->caches[msg_idx].data_out;

	SYS_ERR_COND(status != 0 && status != 0x0d, "bad status (s=%d, t=%d)\n",
		     status, type);

	if (priv->op_type == DEFLATE) {
		if (priv->alg_type == ZLIB) {
			sz = fwrite(ZLIB_HEADER, 1, ZLIB_HEADER_SZ, priv->dfile);
			SYS_ERR_COND(sz != ZLIB_HEADER_SZ, "write zlib hd err");
		} else {
			sz = fwrite(GZIP_HEADER, 1, GZIP_HEADER_SZ, priv->dfile);
			SYS_ERR_COND(sz != GZIP_HEADER_SZ, "write gzip hd err");
			memcpy(gzip_extra + 6, &msg->produced, 4);
			sz = fwrite(gzip_extra, 1, GZIP_EXTRA_SZ, priv->dfile);
			SYS_ERR_COND(sz != GZIP_EXTRA_SZ, "write gzip ex err");
		}
	}
	sz = fwrite(data_out, 1, olen, priv->dfile);
	SYS_ERR_COND(sz != olen, "write data err");
}

int hizip_work(struct hizip_priv *priv, size_t ilen)
{
	int ret;
	size_t olen;
	struct hisi_zip_sqe *msg;

	if (priv->avail_cache && ilen) {
		hizip_read(priv, priv->c_send_idx, ilen);
		do {
			ret = wd_send(&priv->qs[priv->q_send_idx],
				      priv->caches[priv->c_send_idx].msg
				     );
			if (ret == -EBUSY) {
				usleep(1);
				continue;
			} else if (ret < 0) {
				return ret;
			}
		} while (ret);
		priv->q_send_idx = (priv->q_send_idx + 1) % priv->q_num;
		priv->c_send_idx = (priv->c_send_idx + 1) % priv->cache_num;
		priv->avail_cache--;
	} else {
		do {
			ret = wd_recv_sync(&priv->qs[priv->q_recv_idx],
					   &priv->caches[priv->c_recv_idx].msg,
					   1000
					   );
			if ((ret == -EAGAIN) || (ret == -EBUSY)) {
				usleep(1);
				continue;
			} else if (ret < 0) {
				return ret;
			}
		} while (ret);
		msg = priv->caches[priv->c_recv_idx].msg;
		olen = msg->produced;
		hizip_write(priv, priv->c_recv_idx, olen);
		priv->q_recv_idx = (priv->q_recv_idx + 1) % priv->q_num;
		priv->c_recv_idx = (priv->c_recv_idx + 1) % priv->cache_num;
		priv->avail_cache++;
	}
	return priv->avail_cache;
}

int hizip_deflate(FILE *source, FILE *dest)
{
	int fd;
	struct stat s;
	int ret;
	size_t ilen;
	struct hizip_priv *priv;

	priv = &hizip_priv;
	fd = fileno(source);
	SYS_ERR_COND(fstat(fd, &s) < 0, "fstat");
	priv->total_len = s.st_size;
	SYS_ERR_COND(!priv->total_len, "input file length zero");
	priv->sfile = source;
	priv->dfile = dest;

	while ((priv->total_len) || (priv->avail_cache != priv->cache_num)) {
		ilen = priv->total_len > block_size ?
			block_size : priv->total_len;
		priv->total_len -= ilen;

		ret = hizip_work(priv, ilen);
		if (ret < 0)
			return ret;
	}
	return 0;
}

void hizip_def(FILE *source, FILE *dest, int alg_type, int op_type)
{
	int ret;

	ret = hizip_init(alg_type, op_type);
	SYS_ERR_COND(ret, "hizip init fail\n");

	hizip_deflate(stdin, stdout);

	hizip_fini(&hizip_priv);
}
#endif /* WD_SCHED */

int main(int argc, char *argv[])
{
	int alg_type = GZIP;
	int op_type = DEFLATE;
	int opt;
	int show_help = 0;

	while ((opt = getopt(argc, argv, "zghq:b:dc:")) != -1) {
		switch (opt) {
		case 'z':
			alg_type = ZLIB;
			break;
		case 'g':
			alg_type = GZIP;
			break;
		case 'q':
			q_num = atoi(optarg);
			if (q_num <= 0)
				show_help = 1;
			break;
		case 'b':
			block_size = atoi(optarg);
			if (block_size  <= 0)
				show_help = 1;
			break;
		case 'c':
			req_cache_num = atoi(optarg);
			if (req_cache_num <= 0)
				show_help = 1;
			break;
		case 'd':
			op_type = INFLATE;
			SYS_ERR_COND(0, "decompress function to be added\n");
			break;
		default:
			show_help = 1;
			break;
		}
	}

	SYS_ERR_COND(show_help || optind > argc,
		     "test_hisi_zip -[g|z] [-q q_num] < in > out\n");

	hizip_def(stdin, stdout, alg_type, op_type);

	return EXIT_SUCCESS;
}
