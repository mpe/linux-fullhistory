/* Copyright 1996-2000 Hans Reiser, see reiserfs/README for licensing
 * and copyright details */

#ifndef _LINUX_REISER_FS_SB
#define _LINUX_REISER_FS_SB

#ifdef __KERNEL__
#include <linux/tqueue.h>
#endif

//
// super block's field values
//
/*#define REISERFS_VERSION 0 undistributed bitmap */
/*#define REISERFS_VERSION 1 distributed bitmap and resizer*/
#define REISERFS_VERSION_2 2 /* distributed bitmap, resizer, 64-bit, etc*/
#define UNSET_HASH 0 // read_super will guess about, what hash names
                     // in directories were sorted with
#define TEA_HASH  1
#define YURA_HASH 2
#define R5_HASH   3
#define DEFAULT_HASH R5_HASH

/* this is the on disk super block */

struct reiserfs_super_block
{
  __u32 s_block_count;
  __u32 s_free_blocks;                  /* free blocks count    */
  __u32 s_root_block;           	/* root block number    */
  __u32 s_journal_block;           	/* journal block number    */
  __u32 s_journal_dev;           	/* journal device number  */

  /* Since journal size is currently a #define in a header file, if 
  ** someone creates a disk with a 16MB journal and moves it to a 
  ** system with 32MB journal default, they will overflow their journal 
  ** when they mount the disk.  s_orig_journal_size, plus some checks
  ** while mounting (inside journal_init) prevent that from happening
  */

				/* great comment Chris. Thanks.  -Hans */

  __u32 s_orig_journal_size; 		
  __u32 s_journal_trans_max ;           /* max number of blocks in a transaction.  */
  __u32 s_journal_block_count ;         /* total size of the journal. can change over time  */
  __u32 s_journal_max_batch ;           /* max number of blocks to batch into a trans */
  __u32 s_journal_max_commit_age ;      /* in seconds, how old can an async commit be */
  __u32 s_journal_max_trans_age ;       /* in seconds, how old can a transaction be */
  __u16 s_blocksize;                   	/* block size           */
  __u16 s_oid_maxsize;			/* max size of object id array, see get_objectid() commentary  */
  __u16 s_oid_cursize;			/* current size of object id array */
  __u16 s_state;                       	/* valid or error       */
  char s_magic[12];                     /* reiserfs magic string indicates that file system is reiserfs */
  __u32 s_hash_function_code;		/* indicate, what hash function is being use to sort names in a directory*/
  __u16 s_tree_height;                  /* height of disk tree */
  __u16 s_bmap_nr;                      /* amount of bitmap blocks needed to address each block of file system */
  __u16 s_version;		/* I'd prefer it if this was a string,
                                   something like "3.6.4", and maybe
                                   16 bytes long mostly unused. We
                                   don't need to save bytes in the
                                   superblock. -Hans */
  __u16 s_reserved;
  __u32 s_inode_generation;
  char s_unused[124] ;			/* zero filled by mkreiserfs */
} __attribute__ ((__packed__));

#define SB_SIZE (sizeof(struct reiserfs_super_block))
/* struct reiserfs_super_block accessors/mutators
 * since this is a disk structure, it will always be in 
 * little endian format. */
#define sb_block_count(sbp)           (le32_to_cpu((sbp)->s_block_count))
#define set_sb_block_count(sbp,v)     ((sbp)->s_block_count = cpu_to_le32(v))
#define sb_free_blocks(sbp)           (le32_to_cpu((sbp)->s_free_blocks))
#define set_sb_free_blocks(sbp,v)     ((sbp)->s_free_blocks = cpu_to_le32(v))
#define sb_root_block(sbp)            (le32_to_cpu((sbp)->s_root_block))
#define set_sb_root_block(sbp,v)      ((sbp)->s_root_block = cpu_to_le32(v))
#define sb_journal_block(sbp)         (le32_to_cpu((sbp)->s_journal_block))
#define set_sb_journal_block(sbp,v)   ((sbp)->s_journal_block = cpu_to_le32(v))
#define sb_journal_dev(sbp)           (le32_to_cpu((sbp)->s_journal_dev))
#define set_sb_journal_dev(sbp,v)     ((sbp)->s_journal_dev = cpu_to_le32(v))
#define sb_orig_journal_size(sbp)   (le32_to_cpu((sbp)->s_orig_journal_size))
#define set_sb_orig_journal_size(sbp,v) \
                            ((sbp)->s_orig_journal_size = cpu_to_le32(v))
#define sb_journal_trans_max(sbp)     (le32_to_cpu((sbp)->s_journal_trans_max))
#define set_journal_trans_max(sbp,v) \
                            ((sbp)->s_journal_trans_max = cpu_to_le32(v))
#define sb_journal_block_count(sbp)  (le32_to_cpu((sbp)->journal_block_count))
#define sb_set_journal_block_count(sbp,v) \
                            ((sbp)->s_journal_block_count = cpu_to_le32(v))
#define sb_journal_max_batch(sbp)     (le32_to_cpu((sbp)->s_journal_max_batch))
#define set_sb_journal_max_batch(sbp,v) \
                            ((sbp)->s_journal_max_batch = cpu_to_le32(v))
#define sb_jourmal_max_commit_age(sbp) \
                            (le32_to_cpu((sbp)->s_journal_max_commit_age))
#define set_sb_journal_max_commit_age(sbp,v) \
                            ((sbp)->s_journal_max_commit_age = cpu_to_le32(v))
#define sb_jourmal_max_trans_age(sbp) \
                            (le32_to_cpu((sbp)->s_journal_max_trans_age))
#define set_sb_journal_max_trans_age(sbp,v) \
                            ((sbp)->s_journal_max_trans_age = cpu_to_le32(v))
#define sb_blocksize(sbp)             (le16_to_cpu((sbp)->s_blocksize))
#define set_sb_blocksize(sbp,v)       ((sbp)->s_blocksize = cpu_to_le16(v))
#define sb_oid_maxsize(sbp)           (le16_to_cpu((sbp)->s_oid_maxsize))
#define set_sb_oid_maxsize(sbp,v)     ((sbp)->s_oid_maxsize = cpu_to_le16(v))
#define sb_oid_cursize(sbp)           (le16_to_cpu((sbp)->s_oid_cursize))
#define set_sb_oid_cursize(sbp,v)     ((sbp)->s_oid_cursize = cpu_to_le16(v))
#define sb_state(sbp)                 (le16_to_cpu((sbp)->s_state))
#define set_sb_state(sbp,v)           ((sbp)->s_state = cpu_to_le16(v))
#define sb_hash_function_code(sbp) \
                            (le32_to_cpu((sbp)->s_hash_function_code))
#define set_sb_hash_function_code(sbp,v) \
                            ((sbp)->s_hash_function_code = cpu_to_le32(v))
#define sb_tree_height(sbp)           (le16_to_cpu((sbp)->s_tree_height))
#define set_sb_tree_height(sbp,v)     ((sbp)->s_tree_height = cpu_to_le16(v))
#define sb_bmap_nr(sbp)               (le16_to_cpu((sbp)->s_bmap_nr))
#define set_sb_bmap_nr(sbp,v)         ((sbp)->s_bmap_nr = cpu_to_le16(v))
#define sb_version(sbp)               (le16_to_cpu((sbp)->s_version))
#define set_sb_version(sbp,v)         ((sbp)->s_version = cpu_to_le16(v))

/* this is the super from 3.5.X, where X >= 10 */
struct reiserfs_super_block_v1
{
  __u32 s_block_count;			/* blocks count         */
  __u32 s_free_blocks;                  /* free blocks count    */
  __u32 s_root_block;           	/* root block number    */
  __u32 s_journal_block;           	/* journal block number    */
  __u32 s_journal_dev;           	/* journal device number  */
  __u32 s_orig_journal_size; 		/* size of the journal on FS creation.  used to make sure they don't overflow it */
  __u32 s_journal_trans_max ;           /* max number of blocks in a transaction.  */
  __u32 s_journal_block_count ;         /* total size of the journal. can change over time  */
  __u32 s_journal_max_batch ;           /* max number of blocks to batch into a trans */
  __u32 s_journal_max_commit_age ;      /* in seconds, how old can an async commit be */
  __u32 s_journal_max_trans_age ;       /* in seconds, how old can a transaction be */
  __u16 s_blocksize;                   	/* block size           */
  __u16 s_oid_maxsize;			/* max size of object id array, see get_objectid() commentary  */
  __u16 s_oid_cursize;			/* current size of object id array */
  __u16 s_state;                       	/* valid or error       */
  char s_magic[16];                     /* reiserfs magic string indicates that file system is reiserfs */
  __u16 s_tree_height;                  /* height of disk tree */
  __u16 s_bmap_nr;                      /* amount of bitmap blocks needed to address each block of file system */
  __u32 s_reserved;
} __attribute__ ((__packed__));

#define SB_SIZE_V1 (sizeof(struct reiserfs_super_block_v1))

/* LOGGING -- */

/* These all interelate for performance.  
**
** If the journal block count is smaller than n transactions, you lose speed. 
** I don't know what n is yet, I'm guessing 8-16.
**
** typical transaction size depends on the application, how often fsync is
** called, and how many metadata blocks you dirty in a 30 second period.  
** The more small files (<16k) you use, the larger your transactions will
** be.
** 
** If your journal fills faster than dirty buffers get flushed to disk, it must flush them before allowing the journal
** to wrap, which slows things down.  If you need high speed meta data updates, the journal should be big enough
** to prevent wrapping before dirty meta blocks get to disk.
**
** If the batch max is smaller than the transaction max, you'll waste space at the end of the journal
** because journal_end sets the next transaction to start at 0 if the next transaction has any chance of wrapping.
**
** The large the batch max age, the better the speed, and the more meta data changes you'll lose after a crash.
**
*/

/* don't mess with these for a while */
				/* we have a node size define somewhere in reiserfs_fs.h. -Hans */
#define JOURNAL_BLOCK_SIZE  4096 /* BUG gotta get rid of this */
#define JOURNAL_MAX_CNODE   1500 /* max cnodes to allocate. */
#define JOURNAL_TRANS_MAX 1024   /* biggest possible single transaction, don't change for now (8/3/99) */
#define JOURNAL_HASH_SIZE 8192   
#define JOURNAL_NUM_BITMAPS 5 /* number of copies of the bitmaps to have floating.  Must be >= 2 */
#define JOURNAL_LIST_COUNT 64

/* these are bh_state bit flag offset numbers, for use in the buffer head */

#define BH_JDirty       16      /* journal data needs to be written before buffer can be marked dirty */
#define BH_JDirty_wait 18	/* commit is done, buffer marked dirty */
#define BH_JNew 19		/* buffer allocated during this transaction, no need to write if freed during this trans too */

/* ugly.  metadata blocks must be prepared before they can be logged.  
** prepared means unlocked and cleaned.  If the block is prepared, but not
** logged for some reason, any bits cleared while preparing it must be 
** set again.
*/
#define BH_JPrepared 20		/* block has been prepared for the log */
#define BH_JRestore_dirty 22    /* restore the dirty bit later */

/* One of these for every block in every transaction
** Each one is in two hash tables.  First, a hash of the current transaction, and after journal_end, a
** hash of all the in memory transactions.
** next and prev are used by the current transaction (journal_hash).
** hnext and hprev are used by journal_list_hash.  If a block is in more than one transaction, the journal_list_hash
** links it in multiple times.  This allows flush_journal_list to remove just the cnode belonging
** to a given transaction.
*/
struct reiserfs_journal_cnode {
  struct buffer_head *bh ;		 /* real buffer head */
  kdev_t dev ;				 /* dev of real buffer head */
  unsigned long blocknr ;		 /* block number of real buffer head, == 0 when buffer on disk */		 
  int state ;
  struct reiserfs_journal_list *jlist ;  /* journal list this cnode lives in */
  struct reiserfs_journal_cnode *next ;  /* next in transaction list */
  struct reiserfs_journal_cnode *prev ;  /* prev in transaction list */
  struct reiserfs_journal_cnode *hprev ; /* prev in hash list */
  struct reiserfs_journal_cnode *hnext ; /* next in hash list */
};

struct reiserfs_bitmap_node {
  int id ;
  char *data ;
  struct list_head list ;
} ;

struct reiserfs_list_bitmap {
  struct reiserfs_journal_list *journal_list ;
  struct reiserfs_bitmap_node **bitmaps ;
} ;

/*
** transaction handle which is passed around for all journal calls
*/
struct reiserfs_transaction_handle {
				/* ifdef it. -Hans */
  char *t_caller ;              /* debugging use */
  int t_blocks_logged ;         /* number of blocks this writer has logged */
  int t_blocks_allocated ;      /* number of blocks this writer allocated */
  unsigned long t_trans_id ;    /* sanity check, equals the current trans id */
  struct super_block *t_super ; /* super for this FS when journal_begin was 
                                   called. saves calls to reiserfs_get_super */
} ;

/*
** one of these for each transaction.  The most important part here is the j_realblock.
** this list of cnodes is used to hash all the blocks in all the commits, to mark all the
** real buffer heads dirty once all the commits hit the disk,
** and to make sure every real block in a transaction is on disk before allowing the log area
** to be overwritten */
struct reiserfs_journal_list {
  unsigned long j_start ;
  unsigned long j_len ;
  atomic_t j_nonzerolen ;
  atomic_t j_commit_left ;
  atomic_t j_flushing ;
  atomic_t j_commit_flushing ;
  atomic_t j_older_commits_done ;      /* all commits older than this on disk*/
  unsigned long j_trans_id ;
  time_t j_timestamp ;
  struct reiserfs_list_bitmap *j_list_bitmap ;
  struct buffer_head *j_commit_bh ; /* commit buffer head */
  struct reiserfs_journal_cnode *j_realblock  ;
  struct reiserfs_journal_cnode *j_freedlist ; /* list of buffers that were freed during this trans.  free each of these on flush */
  wait_queue_head_t j_commit_wait ; /* wait for all the commit blocks to be flushed */
  wait_queue_head_t j_flush_wait ; /* wait for all the real blocks to be flushed */
} ;

struct reiserfs_page_list  ; /* defined in reiserfs_fs.h */

struct reiserfs_journal {
  struct buffer_head ** j_ap_blocks ; /* journal blocks on disk */
  struct reiserfs_journal_cnode *j_last ; /* newest journal block */
  struct reiserfs_journal_cnode *j_first ; /*  oldest journal block.  start here for traverse */
				
  int j_state ;			
  unsigned long j_trans_id ;
  unsigned long j_mount_id ;
  unsigned long j_start ;             /* start of current waiting commit (index into j_ap_blocks) */
  unsigned long j_len ;               /* lenght of current waiting commit */
  unsigned long j_len_alloc ;         /* number of buffers requested by journal_begin() */
  atomic_t j_wcount ;            /* count of writers for current commit */
  unsigned long j_bcount ;            /* batch count. allows turning X transactions into 1 */
  unsigned long j_first_unflushed_offset ;  /* first unflushed transactions offset */
  unsigned long j_last_flush_trans_id ;    /* last fully flushed journal timestamp */
  struct buffer_head *j_header_bh ;   

  /* j_flush_pages must be flushed before the current transaction can
  ** commit
  */
  struct reiserfs_page_list *j_flush_pages ;
  time_t j_trans_start_time ;         /* time this transaction started */
  wait_queue_head_t j_wait ;         /* wait  journal_end to finish I/O */
  atomic_t j_wlock ;                       /* lock for j_wait */
  wait_queue_head_t j_join_wait ;    /* wait for current transaction to finish before starting new one */
  atomic_t j_jlock ;                       /* lock for j_join_wait */
  int j_journal_list_index ;	      /* journal list number of the current trans */
  int j_list_bitmap_index ;	      /* number of next list bitmap to use */
  int j_must_wait ;		       /* no more journal begins allowed. MUST sleep on j_join_wait */
  int j_next_full_flush ;             /* next journal_end will flush all journal list */
  int j_next_async_flush ;             /* next journal_end will flush all async commits */

  int j_cnode_used ;	      /* number of cnodes on the used list */
  int j_cnode_free ;          /* number of cnodes on the free list */

  struct reiserfs_journal_cnode *j_cnode_free_list ;
  struct reiserfs_journal_cnode *j_cnode_free_orig ; /* orig pointer returned from vmalloc */

  int j_free_bitmap_nodes ;
  int j_used_bitmap_nodes ;
  struct list_head j_bitmap_nodes ;
  struct inode j_dummy_inode ;
  struct reiserfs_list_bitmap j_list_bitmap[JOURNAL_NUM_BITMAPS] ;	/* array of bitmaps to record the deleted blocks */
  struct reiserfs_journal_list j_journal_list[JOURNAL_LIST_COUNT] ;	    /* array of all the journal lists */
  struct reiserfs_journal_cnode *j_hash_table[JOURNAL_HASH_SIZE] ; 	    /* hash table for real buffer heads in current trans */ 
  struct reiserfs_journal_cnode *j_list_hash_table[JOURNAL_HASH_SIZE] ; /* hash table for all the real buffer heads in all 
  										the transactions */
  struct list_head j_prealloc_list;     /* list of inodes which have preallocated blocks */
};

#define JOURNAL_DESC_MAGIC "ReIsErLB" /* ick.  magic string to find desc blocks in the journal */


typedef __u32 (*hashf_t) (const signed char *, int);

struct proc_dir_entry;

#if defined( CONFIG_PROC_FS ) && defined( CONFIG_REISERFS_PROC_INFO )
typedef unsigned long int stat_cnt_t;
typedef struct reiserfs_proc_info_data
{
  spinlock_t lock;
  int exiting;
  int max_hash_collisions;

  stat_cnt_t breads;
  stat_cnt_t bread_miss;
  stat_cnt_t search_by_key;
  stat_cnt_t search_by_key_fs_changed;
  stat_cnt_t search_by_key_restarted;

  stat_cnt_t leaked_oid;
  stat_cnt_t leaves_removable;

  /* balances per level. Use explicit 5 as MAX_HEIGHT is not visible yet. */
  stat_cnt_t balance_at[ 5 ]; /* XXX */
  /* sbk == search_by_key */
  stat_cnt_t sbk_read_at[ 5 ]; /* XXX */
  stat_cnt_t sbk_fs_changed[ 5 ];
  stat_cnt_t sbk_restarted[ 5 ];
  stat_cnt_t items_at[ 5 ]; /* XXX */
  stat_cnt_t free_at[ 5 ]; /* XXX */
  stat_cnt_t can_node_be_removed[ 5 ]; /* XXX */
  long int lnum[ 5 ]; /* XXX */
  long int rnum[ 5 ]; /* XXX */
  long int lbytes[ 5 ]; /* XXX */
  long int rbytes[ 5 ]; /* XXX */
  stat_cnt_t get_neighbors[ 5 ];
  stat_cnt_t get_neighbors_restart[ 5 ];
  stat_cnt_t need_l_neighbor[ 5 ];
  stat_cnt_t need_r_neighbor[ 5 ];

  stat_cnt_t free_block;
  struct __find_forward_stats {
	stat_cnt_t call;
	stat_cnt_t wait;
	stat_cnt_t bmap;
	stat_cnt_t retry;
	stat_cnt_t in_journal_hint;
	stat_cnt_t in_journal_out;
  } find_forward;
  struct __journal_stats {
	stat_cnt_t in_journal;
	stat_cnt_t in_journal_bitmap;
	stat_cnt_t in_journal_reusable;
	stat_cnt_t lock_journal;
	stat_cnt_t lock_journal_wait;
	stat_cnt_t journal_being;
	stat_cnt_t journal_relock_writers;
	stat_cnt_t journal_relock_wcount;
	stat_cnt_t mark_dirty;
	stat_cnt_t mark_dirty_already;
	stat_cnt_t mark_dirty_notjournal;
	stat_cnt_t restore_prepared;
	stat_cnt_t prepare;
	stat_cnt_t prepare_retry;
  } journal;
} reiserfs_proc_info_data_t;
#else
typedef struct reiserfs_proc_info_data
{} reiserfs_proc_info_data_t;
#endif

/* reiserfs union of in-core super block data */
struct reiserfs_sb_info
{
    struct buffer_head * s_sbh;                   /* Buffer containing the super block */
				/* both the comment and the choice of
                                   name are unclear for s_rs -Hans */
    struct reiserfs_super_block * s_rs;           /* Pointer to the super block in the buffer */
    struct buffer_head ** s_ap_bitmap;       /* array of buffers, holding block bitmap */
    struct reiserfs_journal *s_journal ;		/* pointer to journal information */
    unsigned short s_mount_state;                 /* reiserfs state (valid, invalid) */
  
				/* Comment? -Hans */
    void (*end_io_handler)(struct buffer_head *, int);
    hashf_t s_hash_function;	/* pointer to function which is used
                                   to sort names in directory. Set on
                                   mount */
    unsigned long s_mount_opt;	/* reiserfs's mount options are set
                                   here (currently - NOTAIL, NOLOG,
                                   REPLAYONLY) */

				/* Comment? -Hans */
    wait_queue_head_t s_wait;
				/* To be obsoleted soon by per buffer seals.. -Hans */
    atomic_t s_generation_counter; // increased by one every time the
    // tree gets re-balanced
    
    /* session statistics */
    int s_kmallocs;
    int s_disk_reads;
    int s_disk_writes;
    int s_fix_nodes;
    int s_do_balance;
    int s_unneeded_left_neighbor;
    int s_good_search_by_key_reada;
    int s_bmaps;
    int s_bmaps_without_search;
    int s_direct2indirect;
    int s_indirect2direct;
    reiserfs_proc_info_data_t s_proc_info_data;
    struct proc_dir_entry *procdir;
};


#define NOTAIL 0  /* -o notail: no tails will be created in a session */
#define REPLAYONLY 3 /* replay journal and return 0. Use by fsck */
#define REISERFS_NOLOG 4      /* -o nolog: turn journalling off */
#define REISERFS_CONVERT 5    /* -o conv: causes conversion of old
                                 format super block to the new
                                 format. If not specified - old
                                 partition will be dealt with in a
                                 manner of 3.5.x */

/* -o hash={tea, rupasov, r5, detect} is meant for properly mounting 
** reiserfs disks from 3.5.19 or earlier.  99% of the time, this option
** is not required.  If the normal autodection code can't determine which
** hash to use (because both hases had the same value for a file)
** use this option to force a specific hash.  It won't allow you to override
** the existing hash on the FS, so if you have a tea hash disk, and mount
** with -o hash=rupasov, the mount will fail.
*/
#define FORCE_TEA_HASH 6      /* try to force tea hash on mount */
#define FORCE_RUPASOV_HASH 7  /* try to force rupasov hash on mount */
#define FORCE_R5_HASH 8       /* try to force rupasov hash on mount */
#define FORCE_HASH_DETECT 9   /* try to detect hash function on mount */


/* used for testing experimental features, makes benchmarking new
   features with and without more convenient, should never be used by
   users in any code shipped to users (ideally) */

#define REISERFS_NO_BORDER 11
#define REISERFS_NO_UNHASHED_RELOCATION 12
#define REISERFS_HASHED_RELOCATION 13
#define REISERFS_TEST4 14 

#define REISERFS_TEST1 11
#define REISERFS_TEST2 12
#define REISERFS_TEST3 13
#define REISERFS_TEST4 14 

#define reiserfs_r5_hash(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << FORCE_R5_HASH))
#define reiserfs_rupasov_hash(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << FORCE_RUPASOV_HASH))
#define reiserfs_tea_hash(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << FORCE_TEA_HASH))
#define reiserfs_hash_detect(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << FORCE_HASH_DETECT))
#define reiserfs_no_border(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_NO_BORDER))
#define reiserfs_no_unhashed_relocation(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_NO_UNHASHED_RELOCATION))
#define reiserfs_hashed_relocation(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_HASHED_RELOCATION))
#define reiserfs_test4(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_TEST4))

#define dont_have_tails(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << NOTAIL))
#define replay_only(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REPLAYONLY))
#define reiserfs_dont_log(s) ((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_NOLOG))
#define old_format_only(s) ((SB_VERSION(s) != REISERFS_VERSION_2) && !((s)->u.reiserfs_sb.s_mount_opt & (1 << REISERFS_CONVERT)))


void reiserfs_file_buffer (struct buffer_head * bh, int list);
int reiserfs_is_super(struct super_block *s)  ;
int journal_mark_dirty(struct reiserfs_transaction_handle *, struct super_block *, struct buffer_head *bh) ;
int flush_old_commits(struct super_block *s, int) ;
int show_reiserfs_locks(void) ;
int reiserfs_resize(struct super_block *, unsigned long) ;

#define CARRY_ON                0
#define SCHEDULE_OCCURRED       1


#define SB_BUFFER_WITH_SB(s) ((s)->u.reiserfs_sb.s_sbh)
#define SB_JOURNAL(s) ((s)->u.reiserfs_sb.s_journal)
#define SB_JOURNAL_LIST(s) (SB_JOURNAL(s)->j_journal_list)
#define SB_JOURNAL_LIST_INDEX(s) (SB_JOURNAL(s)->j_journal_list_index) 
#define SB_JOURNAL_LEN_FREE(s) (SB_JOURNAL(s)->j_journal_len_free) 
#define SB_AP_BITMAP(s) ((s)->u.reiserfs_sb.s_ap_bitmap)


// on-disk super block fields converted to cpu form
#define SB_DISK_SUPER_BLOCK(s)        ((s)->u.reiserfs_sb.s_rs)
#define SB_BLOCK_COUNT(s)             sb_block_count (SB_DISK_SUPER_BLOCK(s))
#define SB_FREE_BLOCKS(s)             sb_free_blocks (SB_DISK_SUPER_BLOCK(s))
#define SB_REISERFS_MAGIC(s)          (SB_DISK_SUPER_BLOCK(s)->s_magic)
#define SB_ROOT_BLOCK(s)              sb_root_block (SB_DISK_SUPER_BLOCK(s))
#define SB_TREE_HEIGHT(s)             sb_tree_height (SB_DISK_SUPER_BLOCK(s))
#define SB_REISERFS_STATE(s)          sb_state (SB_DISK_SUPER_BLOCK(s))
#define SB_VERSION(s)                 sb_version (SB_DISK_SUPER_BLOCK(s))
#define SB_BMAP_NR(s)                 sb_bmap_nr(SB_DISK_SUPER_BLOCK(s))

#define PUT_SB_BLOCK_COUNT(s, val)    do { set_sb_block_count( SB_DISK_SUPER_BLOCK(s), val); } while (0)
#define PUT_SB_FREE_BLOCKS(s, val)    do { set_sb_free_blocks( SB_DISK_SUPER_BLOCK(s), val); } while (0)
#define PUT_SB_ROOT_BLOCK(s, val)     do { set_sb_root_block( SB_DISK_SUPER_BLOCK(s), val); } while (0)
#define PUT_SB_TREE_HEIGHT(s, val)    do { set_sb_tree_height( SB_DISK_SUPER_BLOCK(s), val); } while (0)
#define PUT_SB_REISERFS_STATE(s, val) do { set_sb_state( SB_DISK_SUPER_BLOCK(s), val); } while (0) 
#define PUT_SB_VERSION(s, val)        do { set_sb_version( SB_DISK_SUPER_BLOCK(s), val); } while (0)
#define PUT_SB_BMAP_NR(s, val)        do { set_sb_bmap_nr( SB_DISK_SUPER_BLOCK(s), val); } while (0)

#endif	/* _LINUX_REISER_FS_SB */
