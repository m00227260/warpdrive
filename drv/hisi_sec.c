/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include "hisi_sec.h"

#define SEC_DIGEST_ALG_OFFSET 11
#define BD_TYPE2 	      0
#define WORD_BYTES	      4
#define SEC_FLAG_OFFSET	      7
#define SEC_HW_TASK_DONE      0x1
#define SEC_DONE_MASK	      0x0001
#define SEC_FLAG_MASK	      0x780
#define SEC_TYPE_MASK	      0x0f

/* should be removed to qm module */
struct hisi_qp_ctx_temp {
	handle_t h_ctx;
	void *sq_base;
	void *cq_base;
	int sqe_size;
	void *mmio_base;
	void *db_base;
	__u16 sq_tail_index;
	__u16 sq_head_index;
	__u16 cq_head_index;
	__u16 sqn;
	bool cqc_phase;
	void *req_cache[QM_Q_DEPTH];
	int is_sq_full;
	int (*db)(struct hisi_qp_ctx_temp *qp_ctx, __u8 cmd, __u16 index,
		  __u8 priority);
};

/* fix me: should be removed to qm module */
struct hisi_qp_ctx_temp *hisi_qm_alloc_qp_ctx_t(handle_t h_ctx)
{
	return NULL;
}

void hisi_qm_free_ctx_t(struct hisi_qp_ctx_temp *qp_ctx)
{
}

int hisi_qm_send_t(struct hisi_qp_ctx_temp *qp_ctx, void *req)
{
	return 0;
}

int hisi_qm_recv_t(struct hisi_qp_ctx_temp *qp_ctx, void **resp)
{
	return 0;
}
/* fix me end */

struct hisi_sec_sess {
	struct hisi_qp_ctx_temp qp_ctx;
	char *node_path;
};

int hisi_sec_init(struct hisi_sec_sess *sec_sess)
{
	/* wd_request_ctx */
	sec_sess->qp_ctx.h_ctx = wd_request_ctx(sec_sess->node_path);
	
	/* alloc_qp_ctx */
	hisi_qm_alloc_qp_ctx_t(sec_sess->qp_ctx.h_ctx);

	/* update qm private info: sqe_size, op_type */

	/* update sec private info: something maybe */

	wd_ctx_start(sec_sess->qp_ctx.h_ctx);

	return 0;
}

void hisi_sec_exit(struct hisi_sec_sess *sec_sess)
{
	/* wd_ctx_stop */

	/* free alloc_qp_ctx */

	/* wd_release_ctx */
}

int hisi_sec_set_key(struct wd_cipher_arg *arg, const __u8 *key, __u32 key_len)
{
	/* store key to sess */
	memcpy(arg->key, key, key_len);
	arg->key_bytes = key_len;

	return 0;
}

void hisi_cipher_create_request(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg,
				struct hisi_sec_sqe *sqe)
{

}

/* should define a struct to pass aead, cipher to this function */
int hisi_sec_encrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	struct hisi_sec_sess *priv;
	struct hisi_sec_sqe msg = {0};
	struct hisi_sec_sqe recv_msg = {0};
	int ret;

	priv = (struct hisi_sec_sess *)sess->priv;
	//fill hisi sec sqe;
	hisi_cipher_create_request(sess, arg, &msg);

	ret = hisi_qm_send_t(&priv->qp_ctx, &msg);
	if (ret == -EBUSY) {
		usleep(1);
	}
	if (ret) {
		WD_ERR("send failed (%d)\n", ret);
		goto out;
	}
recv_again:
	ret = hisi_qm_recv_t(&priv->qp_ctx, (void **)&recv_msg);
	if (ret == -EIO) {
		WD_ERR("wd recv msg failed!\n");
		goto out;
	} else if (ret == -EAGAIN)
		goto recv_again;

	return 0;
out:
	return ret;
}

/* same as above */
int hisi_sec_decrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	return 0;
}

int hisi_cipher_init(struct wd_cipher_sess *sess)
{
	struct hisi_sec_sess *sec_sess;
	int ret = 0;

	sec_sess = calloc(1, sizeof(*sec_sess));
	if (!sec_sess)
		return -ENOMEM;

	sess->priv = sec_sess;
	sec_sess->node_path = strdup(sess->node_path);

	/* fix me: how to do with this? */
	ret = hisi_sec_init(sec_sess);
	if (ret < 0) {
		WD_ERR("hisi sec init failed\n");
		free(sec_sess);
		sess->priv = NULL;
	}

	return ret;
}

void hisi_cipher_exit(struct wd_cipher_sess *sess)
{
	struct hisi_sec_sess *sec_sess = sess->priv;

	hisi_sec_exit(sess->priv);

	free(sec_sess->node_path);
	free(sec_sess);
}

int hisi_cipher_prep(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	return 0;
}

void hisi_cipher_fini(struct wd_cipher_sess *sess)
{
}

int hisi_cipher_set_key(struct wd_cipher_sess *sess, const __u8 *key, __u32 key_len)
{
	return hisi_sec_set_key(sess->priv, key, key_len);
}

int hisi_cipher_encrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	/* this function may be reused by aead, should change to proper inputs */
	return hisi_sec_encrypt(sess, arg);
}

int hisi_cipher_decrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	/* this function may be reused by aead, should change to proper inputs */
	return hisi_sec_decrypt(sess, arg);
}

int hisi_cipher_poll(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg)
{
	return 0;
}

int hisi_digest_init(struct wd_digest_sess *sess)
{
	struct hisi_sec_sess *sec_sess;
	int ret = 0;

	sec_sess = calloc(1, sizeof(*sec_sess));
	if (!sec_sess)
		return -ENOMEM;

	sess->priv = sec_sess;
	sec_sess->node_path = strdup(sess->node_path);

	ret = hisi_sec_init(sec_sess);
	if (ret < 0) {
		free(sec_sess);
		sess->priv = NULL;
	}

	return ret;
}

void hisi_digest_exit(struct wd_digest_sess *sess)
{
	struct hisi_sec_sess *sec_sess = sess->priv;

	hisi_sec_exit(sess->priv);

	free(sec_sess->node_path);
	free(sec_sess);
}

int hisi_digest_prep(struct wd_digest_sess *sess, struct wd_digest_arg *arg)
{
	return 0;
}

void hisi_digest_fini(struct wd_digest_sess *sess)
{
}

int hisi_digest_set_key(struct wd_digest_sess *sess, const __u8 *key, __u32 key_len)
{
	return hisi_sec_set_key(sess->priv, key, key_len);
}

static void qm_fill_digest_alg(struct wd_digest_sess *sess,
			       struct wd_digest_arg *arg,
			       struct hisi_sec_sqe *sqe)
{
	sqe->type2.mac_key_alg = (__u32)(arg->out_bytes / WORD_BYTES);

	if (arg->mode == WD_DIGEST_NORMAL)
		sqe->type2.mac_key_alg |= (__u32)(g_digest_a_alg[sess->alg]) <<
					SEC_DIGEST_ALG_OFFSET;
	else
		sqe->type2.mac_key_alg |= (__u32)(g_hmac_a_alg[sess->alg]) <<
					SEC_DIGEST_ALG_OFFSET;
}

int hisi_digest_digest(struct wd_digest_sess *sess, struct wd_digest_arg *arg)
{
	struct hisi_sec_sess *sec_sess = sess->priv;
	struct hisi_sec_sqe sqe = {0};
	struct hisi_sec_sqe sqe_recv = {0};
	__u16 done, flag;
	__u8 type, etype;

	hisi_digest_create_request(sess, arg, &sqe);

	hisi_qm_send_t(&sec_sess->qp_ctx, &sqe);

	type = sqe_recv.type_auth_cipher & SEC_TYPE_MASK;
	/* error handle */
	done = sqe_recv.type2.done_flag & SEC_DONE_MASK;
	flag = (sqe_recv.type2.done_flag & SEC_FLAG_MASK) >> SEC_FLAG_OFFSET;
	etype = sqe_recv.type2.error_type;

	/* fix me: how to handle parall, some place we need a lock */
	hisi_qm_recv_t(&sec_sess->qp_ctx, &sqe_recv);
	if (type == BD_TYPE2) {
		if (done != SEC_HW_TASK_DONE || etype) {
			WD_ERR("Digest fail! done=0x%x, etype=0x%x\n", done, etype);
			return -EIO;
		}
	}

	return 0;
}

int hisi_digest_poll(struct wd_digest_sess *sess, struct wd_digest_arg *arg)
{
	return 0;
}
