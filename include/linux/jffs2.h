/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * The original JFFS, from which the design for JFFS2 was derived,
 * was designed and implemented by Axis Communications AB.
 *
 * The contents of this file are subject to the Red Hat eCos Public
 * License Version 1.1 (the "Licence"); you may not use this file
 * except in compliance with the Licence.  You may obtain a copy of
 * the Licence at http://www.redhat.com/
 *
 * Software distributed under the Licence is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the Licence for the specific language governing rights and
 * limitations under the Licence.
 *
 * The Original Code is JFFS2 - Journalling Flash File System, version 2
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the RHEPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the RHEPL or the GPL.
 *
 * $Id: jffs2.h,v 1.23 2002/02/21 17:03:45 dwmw2 Exp $
 *
 */

#ifndef __LINUX_JFFS2_H__
#define __LINUX_JFFS2_H__

#define JFFS2_SUPER_MAGIC 0x72b6

/* Values we may expect to find in the 'magic' field */
#define JFFS2_OLD_MAGIC_BITMASK 0x1984
#define JFFS2_MAGIC_BITMASK 0x1985
#define KSAMTIB_CIGAM_2SFFJ 0x5981 /* For detecting wrong-endian fs */
#define JFFS2_EMPTY_BITMASK 0xffff
#define JFFS2_DIRTY_BITMASK 0x0000

/* We only allow a single char for length, and 0xFF is empty flash so
   we don't want it confused with a real length. Hence max 254.
*/
#define JFFS2_MAX_NAME_LEN 254

/* How small can we sensibly write nodes? */
#define JFFS2_MIN_DATA_LEN 128

#define JFFS2_COMPR_NONE	0x00
#define JFFS2_COMPR_ZERO	0x01
#define JFFS2_COMPR_RTIME	0x02
#define JFFS2_COMPR_RUBINMIPS	0x03
#define JFFS2_COMPR_COPY	0x04
#define JFFS2_COMPR_DYNRUBIN	0x05
#define JFFS2_COMPR_ZLIB	0x06
/* Compatibility flags. */
#define JFFS2_COMPAT_MASK 0xc000      /* What do to if an unknown nodetype is found */
#define JFFS2_NODE_ACCURATE 0x2000
/* INCOMPAT: Fail to mount the filesystem */
#define JFFS2_FEATURE_INCOMPAT 0xc000
/* ROCOMPAT: Mount read-only */
#define JFFS2_FEATURE_ROCOMPAT 0x8000
/* RWCOMPAT_COPY: Mount read/write, and copy the node when it's GC'd */
#define JFFS2_FEATURE_RWCOMPAT_COPY 0x4000
/* RWCOMPAT_DELETE: Mount read/write, and delete the node when it's GC'd */
#define JFFS2_FEATURE_RWCOMPAT_DELETE 0x0000

#define JFFS2_NODETYPE_DIRENT (JFFS2_FEATURE_INCOMPAT | JFFS2_NODE_ACCURATE | 1)
#define JFFS2_NODETYPE_INODE (JFFS2_FEATURE_INCOMPAT | JFFS2_NODE_ACCURATE | 2)
#define JFFS2_NODETYPE_CLEANMARKER (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_NODE_ACCURATE | 3)
#define JFFS2_NODETYPE_PADDING (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_NODE_ACCURATE | 4)

// Maybe later...
//#define JFFS2_NODETYPE_CHECKPOINT (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_NODE_ACCURATE | 3)
//#define JFFS2_NODETYPE_OPTIONS (JFFS2_FEATURE_RWCOMPAT_COPY | JFFS2_NODE_ACCURATE | 4)


#define JFFS2_INO_FLAG_PREREAD	  1	/* Do read_inode() for this one at 
					   mount time, don't wait for it to 
					   happen later */
#define JFFS2_INO_FLAG_USERCOMPR  2	/* User has requested a specific 
					   compression type */


struct jffs2_unknown_node
{
	/* All start like this */
	uint16_t magic;
	uint16_t nodetype;
	uint32_t totlen; /* So we can skip over nodes we don't grok */
	uint32_t hdr_crc;
} __attribute__((packed));

struct jffs2_raw_dirent
{
	uint16_t magic;
	uint16_t nodetype;	/* == JFFS_NODETYPE_DIRENT */
	uint32_t totlen;
	uint32_t hdr_crc;
	uint32_t pino;
	uint32_t version;
	uint32_t ino; /* == zero for unlink */
	uint32_t mctime;
	uint8_t nsize;
	uint8_t type;
	uint8_t unused[2];
	uint32_t node_crc;
	uint32_t name_crc;
	uint8_t name[0];
} __attribute__((packed));

/* The JFFS2 raw inode structure: Used for storage on physical media.  */
/* The uid, gid, atime, mtime and ctime members could be longer, but 
   are left like this for space efficiency. If and when people decide
   they really need them extended, it's simple enough to add support for
   a new type of raw node.
*/
struct jffs2_raw_inode
{
	uint16_t magic;      /* A constant magic number.  */
	uint16_t nodetype;   /* == JFFS_NODETYPE_INODE */
	uint32_t totlen;     /* Total length of this node (inc data, etc.) */
	uint32_t hdr_crc;
	uint32_t ino;        /* Inode number.  */
	uint32_t version;    /* Version number.  */
	uint32_t mode;       /* The file's type or mode.  */
	uint16_t uid;        /* The file's owner.  */
	uint16_t gid;        /* The file's group.  */
	uint32_t isize;      /* Total resultant size of this inode (used for truncations)  */
	uint32_t atime;      /* Last access time.  */
	uint32_t mtime;      /* Last modification time.  */
	uint32_t ctime;      /* Change time.  */
	uint32_t offset;     /* Where to begin to write.  */
	uint32_t csize;      /* (Compressed) data size */
	uint32_t dsize;	     /* Size of the node's data. (after decompression) */
	uint8_t compr;       /* Compression algorithm used */
	uint8_t usercompr;   /* Compression algorithm requested by the user */
	uint16_t flags;	     /* See JFFS2_INO_FLAG_* */
	uint32_t data_crc;   /* CRC for the (compressed) data.  */
	uint32_t node_crc;   /* CRC for the raw inode (excluding data)  */
//	uint8_t data[dsize];
} __attribute__((packed));

union jffs2_node_union {
	struct jffs2_raw_inode i;
	struct jffs2_raw_dirent d;
	struct jffs2_unknown_node u;
};

#endif /* __LINUX_JFFS2_H__ */
