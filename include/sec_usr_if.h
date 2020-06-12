/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef HISI_SEC_USR_IF_H
#define HISI_SEC_USR_IF_H

#include <asm/types.h>


struct hisi_sec_sqe {
	__u32 type;
	__u32 cipher;
	__u32 auth;
	__u32 seq;
	__u32 de;
	__u32 scene;
	__u32 src_addr_type;
	__u32 dst_addr_type;
	__u32 mac_addr_type;
	__u32 rsvd0;
	
	struct hisi_sec_sqe_type2 type2;
};

#endif
