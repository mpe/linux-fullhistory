/*
 *  ncp.h
 *
 *  Copyright (C) 1995 by Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_H
#define _LINUX_NCP_H

#include <linux/types.h>
#include <linux/ipx.h>

#define NCP_PTYPE                (0x11)
#define NCP_PORT                 (0x0451)

#define NCP_ALLOC_SLOT_REQUEST   (0x1111)
#define NCP_REQUEST              (0x2222)
#define NCP_DEALLOC_SLOT_REQUEST (0x5555)

struct ncp_request_header {
	__u16   type      __attribute__ ((packed));
	__u8    sequence  __attribute__ ((packed));
	__u8    conn_low  __attribute__ ((packed));
	__u8    task      __attribute__ ((packed));
	__u8    conn_high __attribute__ ((packed));
	__u8    function  __attribute__ ((packed));
	__u8    data[0]   __attribute__ ((packed));
};

#define NCP_REPLY                (0x3333)
#define NCP_POSITIVE_ACK         (0x9999)

struct ncp_reply_header {
	__u16   type             __attribute__ ((packed));
	__u8    sequence         __attribute__ ((packed));
	__u8    conn_low         __attribute__ ((packed));
	__u8    task             __attribute__ ((packed));
	__u8    conn_high        __attribute__ ((packed));
	__u8    completion_code  __attribute__ ((packed));
	__u8    connection_state __attribute__ ((packed));
	__u8    data[0]          __attribute__ ((packed));
};


#define NCP_BINDERY_USER (0x0001)
#define NCP_BINDERY_UGROUP (0x0002)
#define NCP_BINDERY_PQUEUE (0x0003)
#define NCP_BINDERY_FSERVER (0x0004)
#define NCP_BINDERY_NAME_LEN (48)
struct ncp_bindery_object {
	__u32   object_id;
	__u16   object_type;
	__u8    object_name[NCP_BINDERY_NAME_LEN];
	__u8    object_flags;
	__u8    object_security;
	__u8    object_has_prop;
};

struct nw_property {
	__u8    value[128];
	__u8    more_flag;
	__u8    property_flag;
};

struct prop_net_address {
	__u32 network                __attribute__ ((packed));
	__u8  node[IPX_NODE_LEN]     __attribute__ ((packed));
	__u16 port                   __attribute__ ((packed));
};

#define NCP_VOLNAME_LEN (16)
#define NCP_NUMBER_OF_VOLUMES (64)
struct ncp_volume_info {
	__u32   total_blocks;
	__u32   free_blocks;
	__u32   purgeable_blocks;
	__u32   not_yet_purgeable_blocks;
	__u32   total_dir_entries;
	__u32   available_dir_entries;
	__u8    sectors_per_block;
	char    volume_name[NCP_VOLNAME_LEN+1];
};

struct ncp_filesearch_info {
	__u8    volume_number;
	__u16   directory_id;
	__u16   sequence_no;
	__u8    access_rights;
};

#define NCP_MAX_FILENAME 14

/* these define the attribute byte as seen by NCP */
#define aRONLY     (1L<<0)
#define aHIDDEN    (1L<<1)
#define aSYSTEM    (1L<<2)
#define aEXECUTE   (1L<<3)
#define aDIR       (1L<<4)
#define aARCH      (1L<<5)

#define AR_READ      (0x01)
#define AR_WRITE     (0x02)
#define AR_EXCLUSIVE (0x20)

#define NCP_FILE_ID_LEN 6
struct ncp_file_info {
	__u8    file_id[NCP_FILE_ID_LEN];
        char    file_name[NCP_MAX_FILENAME+1];
	__u8    file_attributes;
	__u8    file_mode;
	__u32   file_length;
	__u16   creation_date;
	__u16   access_date;
	__u16   update_date;
	__u16   update_time;
};


/*  Defines for ReturnInformationMask */
#define RIM_NAME	      (0x0001L)
#define RIM_SPACE_ALLOCATED   (0x0002L)
#define RIM_ATTRIBUTES	      (0x0004L)
#define RIM_DATA_SIZE	      (0x0008L)
#define RIM_TOTAL_SIZE	      (0x0010L)
#define RIM_EXT_ATTR_INFO     (0x0020L)
#define RIM_ARCHIVE	      (0x0040L)
#define RIM_MODIFY	      (0x0080L)
#define RIM_CREATION	      (0x0100L)
#define RIM_OWNING_NAMESPACE  (0x0200L)
#define RIM_DIRECTORY	      (0x0400L)
#define RIM_RIGHTS	      (0x0800L)
#define RIM_ALL 	      (0x0FFFL)
#define RIM_COMPRESSED_INFO   (0x80000000L)

/* open/create modes */
#define OC_MODE_OPEN	  0x01
#define OC_MODE_TRUNCATE  0x02
#define OC_MODE_REPLACE   0x02
#define OC_MODE_CREATE	  0x08

/* open/create results */
#define OC_ACTION_NONE	   0x00
#define OC_ACTION_OPEN	   0x01
#define OC_ACTION_CREATE   0x02
#define OC_ACTION_TRUNCATE 0x04
#define OC_ACTION_REPLACE  0x04

/* access rights attributes */
#ifndef AR_READ_ONLY
#define AR_READ_ONLY	   0x0001
#define AR_WRITE_ONLY	   0x0002
#define AR_DENY_READ	   0x0004
#define AR_DENY_WRITE	   0x0008
#define AR_COMPATIBILITY   0x0010
#define AR_WRITE_THROUGH   0x0040
#define AR_OPEN_COMPRESSED 0x0100
#endif

struct nw_info_struct
{
	__u32 spaceAlloc                  __attribute__ ((packed));
	__u32 attributes                  __attribute__ ((packed));
	__u16 flags                       __attribute__ ((packed));
	__u32 dataStreamSize              __attribute__ ((packed));
	__u32 totalStreamSize             __attribute__ ((packed));
	__u16 numberOfStreams             __attribute__ ((packed));
	__u16 creationTime                __attribute__ ((packed));
	__u16 creationDate                __attribute__ ((packed));
	__u32 creatorID                   __attribute__ ((packed));
	__u16 modifyTime                  __attribute__ ((packed));
	__u16 modifyDate                  __attribute__ ((packed));
	__u32 modifierID                  __attribute__ ((packed));
	__u16 lastAccessDate              __attribute__ ((packed));
	__u16 archiveTime                 __attribute__ ((packed));
	__u16 archiveDate                 __attribute__ ((packed));
	__u32 archiverID                  __attribute__ ((packed));
	__u16 inheritedRightsMask         __attribute__ ((packed));
	__u32 dirEntNum                   __attribute__ ((packed));
	__u32 DosDirNum                   __attribute__ ((packed));
	__u32 volNumber                   __attribute__ ((packed));
	__u32 EADataSize                  __attribute__ ((packed));
	__u32 EAKeyCount                  __attribute__ ((packed));
	__u32 EAKeySize                   __attribute__ ((packed));
	__u32 NSCreator                   __attribute__ ((packed));
	__u8  nameLen                     __attribute__ ((packed));
	__u8  entryName[256]              __attribute__ ((packed));
};

/* modify mask - use with MODIFY_DOS_INFO structure */
#define DM_ATTRIBUTES		  (0x0002L)
#define DM_CREATE_DATE		  (0x0004L)
#define DM_CREATE_TIME		  (0x0008L)
#define DM_CREATOR_ID		  (0x0010L)
#define DM_ARCHIVE_DATE 	  (0x0020L)
#define DM_ARCHIVE_TIME 	  (0x0040L)
#define DM_ARCHIVER_ID		  (0x0080L)
#define DM_MODIFY_DATE		  (0x0100L)
#define DM_MODIFY_TIME		  (0x0200L)
#define DM_MODIFIER_ID		  (0x0400L)
#define DM_LAST_ACCESS_DATE	  (0x0800L)
#define DM_INHERITED_RIGHTS_MASK  (0x1000L)
#define DM_MAXIMUM_SPACE	  (0x2000L)

struct nw_modify_dos_info
{
	__u32 attributes                  __attribute__ ((packed));
	__u16 creationDate                __attribute__ ((packed));
	__u16 creationTime                __attribute__ ((packed));
	__u32 creatorID                   __attribute__ ((packed));
	__u16 modifyDate                  __attribute__ ((packed));
	__u16 modifyTime                  __attribute__ ((packed));
	__u32 modifierID                  __attribute__ ((packed));
	__u16 archiveDate                 __attribute__ ((packed));
	__u16 archiveTime                 __attribute__ ((packed));
	__u32 archiverID                  __attribute__ ((packed));
	__u16 lastAccessDate              __attribute__ ((packed));
	__u16 inheritanceGrantMask        __attribute__ ((packed));
	__u16 inheritanceRevokeMask       __attribute__ ((packed));
	__u32 maximumSpace                __attribute__ ((packed));
};

struct nw_file_info {
	struct nw_info_struct i;
	int   opened;
	int   access;
	__u32 server_file_handle          __attribute__ ((packed));
	__u8  open_create_action          __attribute__ ((packed));
	__u8  file_handle[6]              __attribute__ ((packed));
};

struct nw_search_sequence {
	__u8  volNumber                   __attribute__ ((packed));
	__u32 dirBase                     __attribute__ ((packed));
	__u32 sequence                    __attribute__ ((packed));
};

struct nw_queue_job_entry {
	__u16 InUse                       __attribute__ ((packed));
	__u32 prev                        __attribute__ ((packed));
	__u32 next                        __attribute__ ((packed));
	__u32 ClientStation               __attribute__ ((packed));
	__u32 ClientTask                  __attribute__ ((packed));
	__u32 ClientObjectID              __attribute__ ((packed));
	__u32 TargetServerID              __attribute__ ((packed));
	__u8  TargetExecTime[6]           __attribute__ ((packed));
	__u8  JobEntryTime[6]             __attribute__ ((packed));
	__u32 JobNumber                   __attribute__ ((packed));
	__u16 JobType                     __attribute__ ((packed));
	__u16 JobPosition                 __attribute__ ((packed));
	__u16 JobControlFlags             __attribute__ ((packed));
	__u8  FileNameLen                 __attribute__ ((packed));
	char  JobFileName[13]             __attribute__ ((packed));
	__u32 JobFileHandle               __attribute__ ((packed));
	__u32 ServerStation               __attribute__ ((packed));
	__u32 ServerTaskNumber            __attribute__ ((packed));
	__u32 ServerObjectID              __attribute__ ((packed));
	char  JobTextDescription[50]      __attribute__ ((packed));
	char  ClientRecordArea[152]       __attribute__ ((packed));
};

struct queue_job {
	struct nw_queue_job_entry j;
	__u8 file_handle[6];
};

#define QJE_OPER_HOLD	0x80
#define QJE_USER_HOLD	0x40
#define QJE_ENTRYOPEN	0x20
#define QJE_SERV_RESTART    0x10
#define QJE_SERV_AUTO	    0x08

/* ClientRecordArea for print jobs */

#define   KEEP_ON        0x0400
#define   NO_FORM_FEED   0x0800
#define   NOTIFICATION   0x1000
#define   DELETE_FILE    0x2000
#define   EXPAND_TABS    0x4000
#define   PRINT_BANNER   0x8000

struct print_job_record {
    __u8  Version                         __attribute__ ((packed));
    __u8  TabSize                         __attribute__ ((packed));
    __u16 Copies                          __attribute__ ((packed));
    __u16 CtrlFlags                       __attribute__ ((packed));
    __u16 Lines                           __attribute__ ((packed));
    __u16 Rows                            __attribute__ ((packed));
    char  FormName[16]                    __attribute__ ((packed));
    __u8  Reserved[6]                     __attribute__ ((packed));
    char  BannerName[13]                  __attribute__ ((packed));
    char  FnameBanner[13]                 __attribute__ ((packed));
    char  FnameHeader[14]                 __attribute__ ((packed));
    char  Path[80]                        __attribute__ ((packed));
};


#endif /* _LINUX_NCP_H */
