#ifndef	AFFS_HARDBLOCKS_H
#define	AFFS_HARDBLOCKS_H

/* Just the needed definitions for the RDB of an Amiga HD. */

#ifndef AMIGAFFS_H
#include <linux/amigaffs.h>
#endif

struct RigidDiskBlock {
	ULONG	rdb_ID;
	ULONG	rdb_SummedLongs;
	LONG	rdb_ChkSum;
	ULONG	rdb_HostID;
	ULONG	rdb_BlockBytes;
	ULONG	rdb_Flags;
	ULONG	rdb_BadBlockList;
	ULONG	rdb_PartitionList;
	ULONG	rdb_FileSysHeaderList;
	ULONG	rdb_DriveInit;
	ULONG	rdb_Reserved1[6];
	ULONG	rdb_Cylinders;
	ULONG	rdb_Sectors;
	ULONG	rdb_Heads;
	ULONG	rdb_Interleave;
	ULONG	rdb_Park;
	ULONG	rdb_Reserved2[3];
	ULONG	rdb_WritePreComp;
	ULONG	rdb_ReducedWrite;
	ULONG	rdb_StepRate;
	ULONG	rdb_Reserved3[5];
	ULONG	rdb_RDBBlocksLo;
	ULONG	rdb_RDBBlocksHi;
	ULONG	rdb_LoCylinder;
	ULONG	rdb_HiCylinder;
	ULONG	rdb_CylBlocks;
	ULONG	rdb_AutoParkSeconds;
	ULONG	rdb_HighRDSKBlock;
	ULONG	rdb_Reserved4;
	char	rdb_DiskVendor[8];
	char	rdb_DiskProduct[16];
	char	rdb_DiskRevision[4];
	char	rdb_ControllerVendor[8];
	char	rdb_ControllerProduct[16];
	char	rdb_ControllerRevision[4];
	ULONG	rdb_Reserved5[10];
};

#define	IDNAME_RIGIDDISK	0x5244534B	/* "RDSK" */

struct PartitionBlock {
	ULONG	pb_ID;
	ULONG	pb_SummedLongs;
	LONG	pb_ChkSum;
	ULONG	pb_HostID;
	ULONG	pb_Next;
	ULONG	pb_Flags;
	ULONG	pb_Reserved1[2];
	ULONG	pb_DevFlags;
	UBYTE	pb_DriveName[32];
	ULONG	pb_Reserved2[15];
	ULONG	pb_Environment[17];
	ULONG	pb_EReserved[15];
};

#define	IDNAME_PARTITION	0x50415254	/* "PART" */

#define RDB_ALLOCATION_LIMIT	16

#endif	/* AFFS_HARDBLOCKS_H */
