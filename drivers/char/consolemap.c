/*
 * consolemap.c
 *
 * Mapping from internal code (such as Latin-1 or Unicode or IBM PC code)
 * to font positions.
 *
 * aeb, 950210
 */

#include <linux/kd.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include "consolemap.h"

static unsigned char * translations[] = {
/* 8-bit Latin-1 mapped to the PC character set: '\0' means non-printable */
(unsigned char *)
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
	"\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
	"\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
	"\376\245\376\376\376\376\231\376\350\376\376\376\232\376\376\341"
	"\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
	"\376\244\225\242\223\376\224\366\355\227\243\226\201\376\376\230",
/* vt100 graphics */
(unsigned char *)
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^ "
	"\004\261\007\007\007\007\370\361\007\007\331\277\332\300\305\304"
	"\304\304\137\137\303\264\301\302\263\363\362\343\330\234\007\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
	"\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
	"\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
	"\376\245\376\376\376\376\231\376\376\376\376\376\232\376\376\341"
	"\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
	"\376\244\225\242\223\376\224\366\376\227\243\226\201\376\376\230",
/* IBM graphics: minimal translations (BS, CR, LF, LL, SO, SI and ESC) */
(unsigned char *)
	"\000\001\002\003\004\005\006\007\000\011\000\013\000\000\000\000"
	"\020\021\022\023\024\025\026\027\030\031\032\000\034\035\036\037"
	"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
	"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
	"\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117"
	"\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137"
	"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
	"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177"
	"\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217"
	"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"
	"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"
	"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"
	"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"
	"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"
	"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"
	"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377",
 /* USER: customizable mappings, initialized as the previous one (IBM) */
(unsigned char *)
	"\000\001\002\003\004\005\006\007\010\011\000\013\000\000\016\017"
	"\020\021\022\023\024\025\026\027\030\031\032\000\034\035\036\037"
	"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
	"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
	"\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117"
	"\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137"
	"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
	"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177"
	"\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217"
	"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"
	"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"
	"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"
	"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"
	"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"
	"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"
	"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377"
};

/* the above mappings are not invertible - this is just a best effort */
static unsigned char * inv_translate = NULL;
static unsigned char inv_norm_transl[E_TABSZ];
static unsigned char * inverse_translations[4] = { NULL, NULL, NULL, NULL };

static void set_inverse_transl(int i)
{
	int j;
	unsigned char *p = translations[i];
	unsigned char *q = inverse_translations[i];

	if (!q) {
		/* slightly messy to avoid calling kmalloc too early */
		q = inverse_translations[i] = ((i == NORM_MAP)
			? inv_norm_transl
			: (unsigned char *) kmalloc(E_TABSZ, GFP_KERNEL));
		if (!q)
			return;
	}
	for (j=0; j<E_TABSZ; j++)
		q[j] = 0;
	for (j=0; j<E_TABSZ; j++)
		if (q[p[j]] < 32)	/* prefer '-' above SHY etc. */
			q[p[j]] = j;
}

unsigned char *set_translate(int m)
{
	if (!inverse_translations[m])
		set_inverse_transl(m);
	inv_translate = inverse_translations[m];
	return translations[m];
}

/*
 * Inverse translation is impossible for several reasons:
 * 1. The translation maps are not 1-1
 * 2. The text may have been written while a different translation map
 *    was active
 * Still, it is now possible to a certain extent to cut and paste non-ASCII.
 */
unsigned char inverse_translate(unsigned char c) {
	return ((inv_translate && inv_translate[c]) ? inv_translate[c] : c);
}

/*
 * Load customizable translation table
 * arg points to a 256 byte translation table.
 */
int con_set_trans(char * arg)
{
	int i;
	unsigned char *p = translations[USER_MAP];

	i = verify_area(VERIFY_READ, (void *)arg, E_TABSZ);
	if (i)
		return i;

	for (i=0; i<E_TABSZ ; i++)
		p[i] = get_fs_byte(arg+i);
	p[012] = p[014] = p[015] = p[033] = 0;
	set_inverse_transl(USER_MAP);
	return 0;
}

int con_get_trans(char * arg)
{
	int i;
	unsigned char *p = translations[USER_MAP];

	i = verify_area(VERIFY_WRITE, (void *)arg, E_TABSZ);
	if (i)
		return i;

	for (i=0; i<E_TABSZ ; i++) put_fs_byte(p[i],arg+i);
	return 0;
}

/*
 * Unicode -> current font conversion 
 *
 * A font has at most 512 chars, usually 256.
 * But one font position may represent several Unicode chars
 * (and moreover, hashtables work best when they are not too full),
 * so pick HASHSIZE somewhat larger than 512.
 * Since there are likely to be long consecutive stretches
 * (like U+0000 to U+00FF), HASHSTEP should not be too small.
 * Searches longer than MAXHASHLEVEL steps are refused, unless
 * requested explicitly.
 *
 * Note: no conversion tables are compiled in, so the user
 * must supply an explicit mapping herself. See kbd-0.90 (or an
 * earlier kernel version) for the default Unicode-to-PC mapping.
 * Usually, the mapping will be loaded simultaneously with the font.
 */

#define HASHSIZE   641
#define HASHSTEP   189		/* yields hashlevel = 3 initially */
#define MAXHASHLEVEL 6
static struct unipair hashtable[HASHSIZE];

int hashtable_contents_valid = 0; 	/* cleared by setfont */

static unsigned int hashsize;
static unsigned int hashstep;
static unsigned int hashlevel;
static unsigned int maxhashlevel;

void
con_clear_unimap(struct unimapinit *ui) {
	int i;

	/* read advisory values for hash algorithm */
	hashsize = ui->advised_hashsize;
	if (hashsize < 256 || hashsize > HASHSIZE)
	  hashsize = HASHSIZE;
	hashstep = (ui->advised_hashstep % hashsize);
	if (hashstep < 64)
	  hashstep = HASHSTEP;
	maxhashlevel = ui->advised_hashlevel;
	if (!maxhashlevel)
	  maxhashlevel = MAXHASHLEVEL;
	if (maxhashlevel > hashsize)
	  maxhashlevel = hashsize;

	/* initialize */
	hashlevel = 0;
	for (i=0; i<hashsize; i++)
	  hashtable[i].unicode = 0xffff;
	hashtable_contents_valid = 1;
}

int
con_set_unimap(ushort ct, struct unipair *list){
	int i, lct;
	ushort u, hu;
	struct unimapinit hashdefaults = { 0, 0, 0 };

	if (!hashtable_contents_valid)
	  con_clear_unimap(&hashdefaults);
	while(ct) {
	    u = get_fs_word(&list->unicode);
	    i = u % hashsize;
	    lct = 1;
	    while ((hu = hashtable[i].unicode) != 0xffff && hu != u) {
		if (lct++ >=  maxhashlevel)
		  return -ENOMEM;
		i += hashstep;
		if (i >= hashsize)
		  i -= hashsize;
	    }
	    if (lct > hashlevel)
	      hashlevel = lct;
	    hashtable[i].unicode = u;
	    hashtable[i].fontpos = get_fs_word(&list->fontpos);
	    list++;
	    ct--;
	}
	return 0;
}

int
con_get_unimap(ushort ct, ushort *uct, struct unipair *list){
	int i, ect;

	ect = 0;
	if (hashtable_contents_valid)
	  for (i = 0; i<hashsize; i++)
	    if (hashtable[i].unicode != 0xffff) {
		if (ect++ < ct) {
		    put_fs_word(hashtable[i].unicode, &list->unicode);
		    put_fs_word(hashtable[i].fontpos, &list->fontpos);
		    list++;
		}
	    }
	put_fs_word(ect, uct);
	return ((ect <= ct) ? 0 : -ENOMEM);
}

int
conv_uni_to_pc(unsigned long ucs) {
      int i, h;

      if (!hashtable_contents_valid || ucs < 0x20)
	return -3;
      if (ucs == 0xffff || ucs == 0xfffe)
	return -1;
      if (ucs == 0xfeff || (ucs >= 0x200a && ucs <= 0x200f))
	return -2;
      
      h = ucs % hashsize;
      for (i = 0; i < hashlevel; i++) {
	  if (hashtable[h].unicode == ucs)
	    return hashtable[h].fontpos;
	  if ((h += hashstep) >= hashsize)
	    h -= hashsize;
      }

      return -4;		/* not found */
}
