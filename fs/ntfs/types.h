/*
 *  types.h
 *  This file defines four things:
 *   - generic platform independent fixed-size types (e.g. ntfs_u32)
 *   - specific fixed-size types (e.g. ntfs_offset_t)
 *   - macros that read and write those types from and to byte arrays
 *   - types derived from OS specific ones
 *
 *  Copyright (C) 1996 Martin von Löwis
 */

#ifdef NTFS_IN_LINUX_KERNEL
/* get installed types if we compile the kernel*/
#include <linux/fs.h>
#endif

#if defined(i386) || defined(__i386__)

/* unsigned integral types */
#ifndef NTFS_INTEGRAL_TYPES
#define NTFS_INTEGRAL_TYPES
typedef unsigned char      ntfs_u8;
typedef unsigned short     ntfs_u16;
typedef unsigned int       ntfs_u32;
typedef unsigned long long ntfs_u64;
#endif

/* unicode character type */
#ifndef NTFS_WCHAR_T
#define NTFS_WCHAR_T
typedef unsigned short     ntfs_wchar_t;
#endif
/* file offset */
#ifndef NTFS_OFFSET_T
#define NTFS_OFFSET_T
typedef unsigned long long ntfs_offset_t;
#endif
/* UTC */
#ifndef NTFS_TIME64_T
#define NTFS_TIME64_T
typedef unsigned long long ntfs_time64_t;
#endif
/* This is really unsigned long long. So we support only volumes up to 2 TB */
#ifndef NTFS_CLUSTER_T
#define NTFS_CLUSTER_T
typedef unsigned int ntfs_cluster_t;
#endif

/* Macros reading unsigned integers from a byte pointer */
/* these should work for all little endian machines */
#define NTFS_GETU8(p)      (*(ntfs_u8*)(p))
#define NTFS_GETU16(p)     (*(ntfs_u16*)(p))
#define NTFS_GETU24(p)     (NTFS_GETU32(p) & 0xFFFFFF)
#define NTFS_GETU32(p)     (*(ntfs_u32*)(p))
#define NTFS_GETU64(p)     (*(ntfs_u64*)(p))

/* Macros writing unsigned integers */
#define NTFS_PUTU8(p,v)      (*(ntfs_u8*)(p))=(v)
#define NTFS_PUTU16(p,v)     (*(ntfs_u16*)(p))=(v)
#define NTFS_PUTU24(p,v)     NTFS_PUTU16(p,(v) & 0xFFFF);\
                             NTFS_PUTU8(((char*)p)+2,(v)>>16)
#define NTFS_PUTU32(p,v)     (*(ntfs_u32*)(p))=(v)
#define NTFS_PUTU64(p,v)     (*(ntfs_u64*)(p))=(v)

/* Macros reading signed integers, returning int */
#define NTFS_GETS8(p)        ((int)(*(char*)(p)))
#define NTFS_GETS16(p)       ((int)(*(short*)(p)))
#define NTFS_GETS24(p)       (NTFS_GETU24(p) < 0x800000 ? (int)NTFS_GETU24(p) : (int)(NTFS_GETU24(p) | 0xFF000000))

#define NTFS_PUTS8(p,v)      NTFS_PUTU8(p,v)
#define NTFS_PUTS16(p,v)     NTFS_PUTU16(p,v)
#define NTFS_PUTS24(p,v)     NTFS_PUTU24(p,v)
#define NTFS_PUTS32(p,v)     NTFS_PUTU32(p,v)

#else
#error Put your machine description here
#endif

/* architecture independent macros */

/* PUTU32 would not clear all bytes */
#define NTFS_PUTINUM(p,i)    NTFS_PUTU64(p,i->i_number);\
                             NTFS_PUTU16(((char*)p)+6,i->sequence_number)

/* system dependent types */
#ifdef __linux__
/* We always need kernel types, because glibc makes them of different size */
#include <asm/posix_types.h>
/* Avoid a type redefinition with future include of glibc <stdlib.h> */
#undef __FD_ZERO
#undef __FD_SET
#undef __FD_CLR
#undef __FD_ISSET
#ifndef NTMODE_T
#define NTMODE_T
typedef __kernel_mode_t ntmode_t;
#endif
#ifndef NTFS_UID_T
#define NTFS_UID_T
typedef __kernel_uid_t ntfs_uid_t;
#endif
#ifndef NTFS_GID_T
#define NTFS_GID_T
typedef __kernel_gid_t ntfs_gid_t;
#endif
#ifndef NTFS_SIZE_T
#define NTFS_SIZE_T
typedef __kernel_size_t ntfs_size_t;
#endif
#ifndef NTFS_TIME_T
#define NTFS_TIME_T
typedef __kernel_time_t ntfs_time_t;
#endif
#else
#include <sys/types.h>
#include <sys/stat.h>
typedef mode_t ntmode_t;
typedef uid_t ntfs_uid_t;
typedef gid_t ntfs_gid_t;
typedef size_t ntfs_size_t;
typedef time_t ntfs_time_t;
#endif

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
