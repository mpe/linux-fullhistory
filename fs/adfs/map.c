/*
 *  linux/fs/adfs/map.c
 *
 * Copyright (C) 1997 Russell King
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>

static inline unsigned int
adfs_convert_map_to_sector (const struct super_block *sb, unsigned int mapoff)
{
	if (sb->u.adfs_sb.s_map2blk >= 0)
		mapoff <<= sb->u.adfs_sb.s_map2blk;
	else
		mapoff >>= -sb->u.adfs_sb.s_map2blk;
	return mapoff;
}

static inline unsigned int
adfs_convert_sector_to_map (const struct super_block *sb, unsigned int secoff)
{
	if (sb->u.adfs_sb.s_map2blk >= 0)
		secoff >>= sb->u.adfs_sb.s_map2blk;
	else
		secoff <<= -sb->u.adfs_sb.s_map2blk;
	return secoff;
}

static int lookup_zone (struct super_block *sb, int zone, int frag_id, int *offset)
{
	unsigned int mapptr, idlen, mapsize;
	unsigned long *map;

	map     = ((unsigned long *)sb->u.adfs_sb.s_map[zone]->b_data) + 1;
	zone    =
	mapptr  = zone == 0 ? (ADFS_DR_SIZE << 3) : 0;
	idlen   = sb->u.adfs_sb.s_idlen;
	mapsize = sb->u.adfs_sb.s_zonesize;

	do {
		unsigned long v1, v2;
		unsigned int start;

		v1 = map[mapptr>>5];
		v2 = map[(mapptr>>5)+1];

		v1 = (v1 >> (mapptr & 31)) | (v2 << (32 - (mapptr & 31)));
		start = mapptr;
		mapptr += idlen;

		v2 = map[mapptr >> 5] >> (mapptr & 31);
		if (!v2) {
			mapptr = (mapptr + 32) & ~31;
			for (; (v2 = map[mapptr >> 5]) == 0 && mapptr < mapsize; mapptr += 32);
		}
		for (; (v2 & 255) == 0; v2 >>= 8, mapptr += 8);
		for (; (v2 & 1) == 0; v2 >>= 1, mapptr += 1);
		mapptr += 1;

		if ((v1 & ((1 << idlen) - 1)) == frag_id) {
			int length = mapptr - start;
			if (*offset >= length)
				*offset -= length;
			else
				return start + *offset - zone;
		}
	} while (mapptr < mapsize);
	return -1;
}

int adfs_map_lookup (struct super_block *sb, int frag_id, int offset)
{
	unsigned int start_zone, zone, max_zone, mapoff, secoff;

	zone = start_zone = frag_id / sb->u.adfs_sb.s_ids_per_zone;
	max_zone = sb->u.adfs_sb.s_map_size;

	if (start_zone >= max_zone) {
		adfs_error (sb, "adfs_map_lookup", "fragment %X is invalid (zone = %d, max = %d)",
			    frag_id, start_zone, max_zone);
		return 0;
	}

	/* Convert sector offset to map offset */
	mapoff = adfs_convert_sector_to_map (sb, offset);
	/* Calculate sector offset into map block */
	secoff = offset - adfs_convert_map_to_sector (sb, mapoff);

	do {
		int result = lookup_zone (sb, zone, frag_id, &mapoff);

		if (result != -1) {
			result += zone ? (zone * sb->u.adfs_sb.s_zonesize) - (ADFS_DR_SIZE << 3): 0;
			return adfs_convert_map_to_sector (sb, result) + secoff;
		}

		zone ++;
		if (zone >= max_zone)
			zone = 0;

	} while (zone != start_zone);

	adfs_error (sb, "adfs_map_lookup", "fragment %X at offset %d not found in map (start zone %d)",
		    frag_id, offset, start_zone);
	return 0;
}
