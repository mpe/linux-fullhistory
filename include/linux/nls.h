#include <linux/init.h>

struct nls_unicode {
	unsigned char uni1;
	unsigned char uni2;
};

struct nls_table {
	char *charset;
	unsigned char **page_uni2charset;
	struct nls_unicode *charset2uni;
	unsigned char *charset2lower;
	unsigned char *charset2upper;
	struct module *owner;
	struct nls_table *next;
};

/* nls.c */
extern int register_nls(struct nls_table *);
extern int unregister_nls(struct nls_table *);
extern struct nls_table *load_nls(char *);
extern void unload_nls(struct nls_table *);
extern struct nls_table *load_nls_default(void);

extern int utf8_mbtowc(__u16 *, const __u8 *, int);
extern int utf8_mbstowcs(__u16 *, const __u8 *, int);
extern int utf8_wctomb(__u8 *, __u16, int);
extern int utf8_wcstombs(__u8 *, const __u16 *, int);
