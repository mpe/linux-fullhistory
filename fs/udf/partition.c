/*
 * partition.c
 *
 * PURPOSE
 *      Partition handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *      E-mail regarding any portion of the Linux UDF file system should be
 *      directed to the development team mailing list (run by majordomo):
 *              linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *      This file is distributed under the terms of the GNU General Public
 *      License (GPL). Copies of the GPL can be obtained from:
 *              ftp://prep.ai.mit.edu/pub/gnu/GPL
 *      Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-1999 Ben Fennema
 *
 * HISTORY
 *
 * 12/06/98 blf  Created file. 
 *
 */

#include "udfdecl.h"
#include "udf_sb.h"
#include "udf_i.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/udf_fs.h>

extern Uint32 udf_get_pblock(struct super_block *sb, Uint32 block, Uint16 partition, Uint32 offset)
{
	Uint16 ident;

	if (partition >= UDF_SB_NUMPARTS(sb))
	{
		udf_debug("block=%d, partition=%d, offset=%d: invalid partition\n",
			block, partition, offset);
		return 0xFFFFFFFF;
	}
	switch (UDF_SB_PARTTYPE(sb, partition))
	{
		case UDF_TYPE1_MAP15:
		{
			return UDF_SB_PARTROOT(sb, partition) + block + offset;
		}
		case UDF_VIRTUAL_MAP15:
		case UDF_VIRTUAL_MAP20:
		{
			struct buffer_head *bh = NULL;
			Uint32 newblock;
			Uint32 index;
			Uint32 loc;

			index = (sb->s_blocksize - UDF_SB_TYPEVIRT(sb,partition).s_start_offset) / sizeof(Uint32);


			if (block > UDF_SB_TYPEVIRT(sb,partition).s_num_entries)
			{
				udf_debug("Trying to access block beyond end of VAT (%d max %d)\n",
					block, UDF_SB_TYPEVIRT(sb,partition).s_num_entries);
				return 0xFFFFFFFF;
			}

			if (block >= index)
			{
				block -= index;
				newblock = 1 + (block / (sb->s_blocksize / sizeof(Uint32)));
				index = block % (sb->s_blocksize / sizeof(Uint32));
			}
			else
			{
				newblock = 0;
				index = UDF_SB_TYPEVIRT(sb,partition).s_start_offset / sizeof(Uint32) + block;
			}

			loc = udf_locked_block_map(UDF_SB_VAT(sb), newblock);

			if (!(bh = bread(sb->s_dev, loc, sb->s_blocksize)))
			{
				udf_debug("get_pblock(UDF_VIRTUAL_MAP:%p,%d,%d) VAT: %d[%d]\n",
					sb, block, partition, loc, index);
				return 0xFFFFFFFF;
			}

			loc = le32_to_cpu(((Uint32 *)bh->b_data)[index]);

			udf_release_data(bh);

			if (UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum == partition)
			{
				udf_debug("recursive call to udf_get_pblock!\n");
				return 0xFFFFFFFF;
			}

			return udf_get_pblock(sb, loc, UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum, offset);
		}
		case UDF_SPARABLE_MAP15:
		{
			Uint32 newblock = UDF_SB_PARTROOT(sb, partition) + block + offset;
			Uint32 spartable = UDF_SB_TYPESPAR(sb, partition).s_spar_loc;
			Uint32 plength = UDF_SB_TYPESPAR(sb,partition).s_spar_plen;
			Uint32 packet = (block + offset) & (~(plength-1));
			struct buffer_head *bh = NULL;
			struct SparingTable *st;
			SparingEntry *se;

			bh = udf_read_tagged(sb, spartable, spartable, &ident);

			if (!bh)
			{
				printk(KERN_ERR "udf: udf_read_tagged(%p,%d,%d)\n",
					sb, spartable, spartable);
				return 0xFFFFFFFF;
			}

			st = (struct SparingTable *)bh->b_data;
			if (ident == 0)
			{
				if (!strncmp(st->sparingIdent.ident, UDF_ID_SPARING, strlen(UDF_ID_SPARING)))
				{
					Uint16 rtl = le16_to_cpu(st->reallocationTableLen);
					Uint16 index;

					/* If the sparing table span multiple blocks, find out which block we are on */

					se = &(st->mapEntry[0]);

					if (rtl * sizeof(SparingEntry) + sizeof(struct SparingTable) > sb->s_blocksize)
					{
						index = (sb->s_blocksize - sizeof(struct SparingTable)) / sizeof(SparingEntry);
						if (le32_to_cpu(se[index-1].origLocation) == packet)
						{
							udf_release_data(bh);
							return le32_to_cpu(se[index].mappedLocation) | (newblock & (plength-1));
						}
						else if (le32_to_cpu(se[index-1].origLocation) < packet)
						{
							do
							{
								udf_release_data(bh);
								bh = udf_tread(sb, spartable, sb->s_blocksize);
								if (!bh)
									return 0xFFFFFFFF;
								se = (SparingEntry *)bh->b_data;
								spartable ++;
								rtl -= index;
								index = sb->s_blocksize / sizeof(SparingEntry);

								if (le32_to_cpu(se[index].origLocation) == packet)
								{
									udf_release_data(bh);
									return le32_to_cpu(se[index].mappedLocation) | (newblock & (plength-1));
								}
							} while (rtl * sizeof(SparingEntry) > sb->s_blocksize && 
								le32_to_cpu(se[index-1].origLocation) < packet);
						}
					}
			
					for (index=0; index<rtl; index++)
					{
						if (le32_to_cpu(se[index].origLocation) == packet)
						{
							udf_release_data(bh);
							return le32_to_cpu(se[index].mappedLocation) | (newblock & (plength-1));
						}
						else if (le32_to_cpu(se[index].origLocation) > packet)
						{
							udf_release_data(bh);
							return newblock;
						}
					}

					udf_release_data(bh);
					return newblock;
				}
			}
			udf_release_data(bh);
		}
	}
	return 0xFFFFFFFF;
}

extern Uint32 udf_get_lb_pblock(struct super_block *sb, lb_addr loc, Uint32 offset)
{
	return udf_get_pblock(sb, loc.logicalBlockNum, loc.partitionReferenceNum, offset);
}
