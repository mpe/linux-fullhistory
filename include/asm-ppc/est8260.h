
#define IMAP_ADDR	((uint)0xf0000000)


/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
	unsigned int	bi_intfreq;	/* Internal Freq, in Hz */
	unsigned int	bi_busfreq;	/* Bus Freq, in MHz */
	unsigned int	bi_cpmfreq;	/* CPM Freq, in MHz */
	unsigned int	bi_brgfreq;	/* BRG Freq, in MHz */
	unsigned char	bi_enetaddr[6];
	unsigned int	bi_baudrate;
} bd_t;

extern bd_t m8xx_board_info;

