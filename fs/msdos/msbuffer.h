/* buffer.c 13/12/94 20.19.10 */
struct buffer_head *msdos_bread (struct super_block *sb, int block);
struct buffer_head *msdos_getblk (struct super_block *sb, int block);
void msdos_brelse (struct super_block *sb, struct buffer_head *bh);
void msdos_mark_buffer_dirty (struct super_block *sb,
	 struct buffer_head *bh,
	 int dirty_val);
void msdos_set_uptodate (struct super_block *sb,
	 struct buffer_head *bh,
	 int val);
int msdos_is_uptodate (struct super_block *sb, struct buffer_head *bh);
void msdos_ll_rw_block (struct super_block *sb, int opr,
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

#define brelse(b)				msdos_brelse(sb,b)
#define bread(d,b,s)			msdos_bread(sb,b)
#define getblk(d,b,s)			msdos_getblk(sb,b)
#define mark_buffer_dirty(b,v)	msdos_mark_buffer_dirty(sb,b,v)

