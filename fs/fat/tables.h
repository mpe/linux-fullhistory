struct unicode_value {
	unsigned char uni1;
	unsigned char uni2;
};

extern unsigned char fat_a2alias[];		/* Ascii to alias name conversion table */
extern struct unicode_value fat_a2uni[];	/* Ascii to Unicode conversion table */

extern unsigned char *fat_uni2asc_pg[];

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
