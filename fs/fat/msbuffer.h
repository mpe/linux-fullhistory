
struct buffer_head *fat_bread (struct super_block *sb, int block);
struct buffer_head *fat_getblk (struct super_block *sb, int block);
void fat_brelse (struct super_block *sb, struct buffer_head *bh);
void fat_mark_buffer_dirty (struct super_block *sb,
	 struct buffer_head *bh,
	 int dirty_val);
void fat_set_uptodate (struct super_block *sb,
	 struct buffer_head *bh,
	 int val);
int fat_is_uptodate (struct super_block *sb, struct buffer_head *bh);
void fat_ll_rw_block (struct super_block *sb, int opr,
	int nbreq, struct buffer_head *bh[32]);

/* These macros exist to avoid modifying all the code */
/* They should be removed one day I guess */

/* The versioning mechanism of the modules system define those macros */
/* This remove some warnings */
#ifdef brelse
	#undef brelse
#endif
#ifdef bread
	#undef bread
#endif
#ifdef getblk
	#undef getblk
#endif

#define brelse(b)			fat_brelse(sb,b)
#define bread(d,b,s)			fat_bread(sb,b)
#define getblk(d,b,s)			fat_getblk(sb,b)
#define mark_buffer_dirty(b,v)		fat_mark_buffer_dirty(sb,b,v)

