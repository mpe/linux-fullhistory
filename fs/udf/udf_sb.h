#ifndef __LINUX_UDF_SB_H
#define __LINUX_UDF_SB_H

/* Since UDF 1.50 is ISO 13346 based... */
#define UDF_SUPER_MAGIC	0x15013346

#define UDF_FLAG_STRICT		0x00000001U
#define UDF_FLAG_UNDELETE	0x00000002U
#define UDF_FLAG_UNHIDE		0x00000004U
#define UDF_FLAG_VARCONV	0x00000008U

#define UDF_SB_FREE(X)\
{\
	if (UDF_SB(X))\
	{\
		if (UDF_SB_PARTMAPS(X))\
			kfree(UDF_SB_PARTMAPS(X));\
		UDF_SB_PARTMAPS(X) = NULL;\
	}\
}
#define UDF_SB(X)	(&((X)->u.udf_sb))

#define UDF_SB_ALLOC_PARTMAPS(X,Y)\
{\
	UDF_SB_NUMPARTS(X) = Y;\
	UDF_SB_PARTMAPS(X) = kmalloc(sizeof(struct udf_part_map) * Y, GFP_KERNEL);\
}

#define IS_STRICT(X)			( UDF_SB(X)->s_flags & UDF_FLAG_STRICT )
#define IS_UNDELETE(X)			( UDF_SB(X)->s_flags & UDF_FLAG_UNDELETE )
#define IS_UNHIDE(X)			( UDF_SB(X)->s_flags & UDF_FLAG_UNHIDE )

#define UDF_SB_SESSION(X)		( UDF_SB(X)->s_session )
#define UDF_SB_ANCHOR(X)		( UDF_SB(X)->s_anchor )
#define UDF_SB_NUMPARTS(X)		( UDF_SB(X)->s_partitions )
#define UDF_SB_VOLUME(X)		( UDF_SB(X)->s_thisvolume )
#define UDF_SB_LASTBLOCK(X)		( UDF_SB(X)->s_lastblock )
#define UDF_SB_VOLDESC(X)		( UDF_SB(X)->s_voldesc )
#define UDF_SB_LVIDBH(X)		( UDF_SB(X)->s_lvidbh )
#define UDF_SB_LVID(X)			( (struct LogicalVolIntegrityDesc *)UDF_SB_LVIDBH(X)->b_data )
#define UDF_SB_LVIDIU(X)		( (struct LogicalVolIntegrityDescImpUse *)&(UDF_SB_LVID(sb)->impUse[UDF_SB_LVID(sb)->numOfPartitions * 2 * sizeof(Uint32)/sizeof(Uint8)]) )
#define UDF_SB_PARTITION(X)		( UDF_SB(X)->s_partition )
#define UDF_SB_RECORDTIME(X)	( UDF_SB(X)->s_recordtime )
#define UDF_SB_VOLIDENT(X)		( UDF_SB(X)->s_volident )
#define UDF_SB_PARTMAPS(X)		( UDF_SB(X)->s_partmaps )
#define UDF_SB_LOCATION(X)		( UDF_SB(X)->s_location )
#define UDF_SB_SERIALNUM(X)		( UDF_SB(X)->s_serialnum )
#define UDF_SB_CHARSET(X)		( UDF_SB(X)->s_nls_iocharset )
#define UDF_SB_VAT(X)			( UDF_SB(X)->s_vat )

#define UDF_SB_BLOCK_BITMAP_NUMBER(X,Y) ( UDF_SB(X)->s_block_bitmap_number[Y] )
#define UDF_SB_BLOCK_BITMAP(X,Y)		( UDF_SB(X)->s_block_bitmap[Y] )
#define UDF_SB_LOADED_BLOCK_BITMAPS(X)	( UDF_SB(X)->s_loaded_block_bitmaps )

#define UDF_SB_PARTTYPE(X,Y)	( UDF_SB_PARTMAPS(X)[Y].s_partition_type )
#define UDF_SB_PARTROOT(X,Y)	( UDF_SB_PARTMAPS(X)[Y].s_partition_root )
#define UDF_SB_PARTLEN(X,Y)		( UDF_SB_PARTMAPS(X)[Y].s_partition_len )
#define UDF_SB_PARTVSN(X,Y)		( UDF_SB_PARTMAPS(X)[Y].s_volumeseqnum )
#define UDF_SB_PARTNUM(X,Y)		( UDF_SB_PARTMAPS(X)[Y].s_partition_num )
#define UDF_SB_TYPESPAR(X,Y)	( UDF_SB_PARTMAPS(X)[Y].s_type_specific.s_sparing )
#define UDF_SB_TYPEVIRT(X,Y)	( UDF_SB_PARTMAPS(X)[Y].s_type_specific.s_virtual )

#endif /* __LINUX_UDF_SB_H */
