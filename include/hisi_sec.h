/* SPDX-License-Identifier: Apache-2.0 */
#ifndef	__HISI_SEC_H
#define	__HISI_SEC_H

#include "hisi_qm_udrv.h"
#include "include/sec_usr_if.h"
#include "wd.h"
#include "wd_cipher.h"
#include "wd_digest.h"

enum C_ALG {
	C_ALG_DES  = 0x0,
	C_ALG_3DES = 0x1,
	C_ALG_AES  = 0x2,
	C_ALG_SM4  = 0x3,
};

enum A_ALG {
	A_ALG_SHA1	 = 0x0,
	A_ALG_SHA256 = 0x1,
	A_ALG_MD5	 = 0x2,
	A_ALG_SHA224 = 0x3,
	A_ALG_SHA384 = 0x4,
	A_ALG_SHA512 = 0x5,
	A_ALG_SHA512_224 = 0x6,
	A_ALG_SHA512_256 = 0x7,
	A_ALG_HMAC_SHA1   = 0x10,
	A_ALG_HMAC_SHA256 = 0x11,
	A_ALG_HMAC_MD5	  = 0x12,
	A_ALG_HMAC_SHA224 = 0x13,
	A_ALG_HMAC_SHA384 = 0x14,
	A_ALG_HMAC_SHA512 = 0x15,
	A_ALG_HMAC_SHA512_224 = 0x16,
	A_ALG_HMAC_SHA512_256 = 0x17,
	A_ALG_AES_XCBC_MAC_96  = 0x20,
	A_ALG_AES_XCBC_PRF_128 = 0x20,
	A_ALG_AES_CMAC = 0x21,
	A_ALG_AES_GMAC = 0x22,
	A_ALG_SM3	   = 0x25,
	A_ALG_HMAC_SM3 = 0x26
};

enum C_MODE {
	C_MODE_ECB	  = 0x0,
	C_MODE_CBC	  = 0x1,
	C_MODE_CTR	  = 0x4,
	C_MODE_CCM	  = 0x5,
	C_MODE_GCM	  = 0x6,
	C_MODE_XTS	  = 0x7,
	C_MODE_CBC_CS     = 0x9,
};

static int g_digest_a_alg[WD_DIGEST_TYPE_MAX] = {
	0, A_ALG_SM3, A_ALG_MD5, A_ALG_SHA1, A_ALG_SHA256, A_ALG_SHA224,
	A_ALG_SHA384, A_ALG_SHA512, A_ALG_SHA512_224, A_ALG_SHA512_256
};
static int g_hmac_a_alg[WD_DIGEST_TYPE_MAX] = {
	0, A_ALG_HMAC_SM3, A_ALG_HMAC_MD5, A_ALG_HMAC_SHA1,
	A_ALG_HMAC_SHA256, A_ALG_HMAC_SHA224, A_ALG_HMAC_SHA384,
	A_ALG_HMAC_SHA512, A_ALG_HMAC_SHA512_224, A_ALG_HMAC_SHA512_256
};

extern int hisi_cipher_init(struct wd_cipher_sess *sess);
extern void hisi_cipher_exit(struct wd_cipher_sess *sess);
extern int hisi_cipher_prep(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg);
extern void hisi_cipher_fini(struct wd_cipher_sess *sess);
extern int hisi_cipher_set_key(struct wd_cipher_sess *sess, const __u8 *key, __u32 key_len);
extern int hisi_cipher_encrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg);
extern int hisi_cipher_decrypt(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg);
extern int hisi_cipher_poll(struct wd_cipher_sess *sess, struct wd_cipher_arg *arg);

extern int hisi_digest_init(struct wd_digest_sess *sess);
extern void hisi_digest_exit(struct wd_digest_sess *sess);
extern int hisi_digest_prep(struct wd_digest_sess *sess, struct wd_digest_arg *arg);
extern void hisi_digest_fini(struct wd_digest_sess *sess);
extern int hisi_digest_set_key(struct wd_digest_sess *sess, const __u8 *key, __u32 key_len);
extern int hisi_digest_digest(struct wd_digest_sess *sess, struct wd_digest_arg *arg);
extern int hisi_digest_poll(struct wd_digest_sess *sess, struct wd_digest_arg *arg);

#endif	/* __HISI_SEC_H */
