/*
 * Definitions for the GMAC ethernet chip, used in the
 * Apple G4 powermac.
 */

/* Register offsets */
#define	INTR_STATUS		0x000c
#define INTR_DISABLE		0x0010
#define INTR_ACK		0x0014
#define SW_RESET		0x1010
#define TXDMA_KICK		0x2000
#define TXDMA_CONFIG		0x2004
#define TXDMA_BASE_LOW		0x2008
#define TXDMA_BASE_HIGH		0x200c
#define TXDMA_STATE_MACH	0x2028
#define TXDMA_COMPLETE		0x2100
#define RXDMA_CONFIG		0x4000
#define RXDMA_BASE_LOW		0x4004
#define RXDMA_BASE_HIGH		0x4008
#define RXDMA_KICK		0x4100
#define MACPAUSE		0x6008
#define TXMAC_STATUS		0x6010
#define TXMAC_CONFIG		0x6030
#define RXMAC_CONFIG		0x6034
#define MACCNTL_CONFIG		0x6038
#define XIF_CONFIG		0x603c
#define IPG0			0x6040
#define IPG1			0x6044
#define IPG2			0x6048
#define SLOTTIME		0x604c
#define MINFRAMESIZE		0x6050
#define MAXFRAMESIZE		0x6054
#define PASIZE			0x6058
#define JAMSIZE			0x605c
#define ATTEMPT_LIMIT		0x6060
#define MACCNTL_TYPE		0x6064
#define MAC_ADDR_0		0x6080
#define MAC_ADDR_1		0x6084
#define MAC_ADDR_2		0x6088
#define MAC_ADDR_3		0x608c
#define MAC_ADDR_4		0x6090
#define MAC_ADDR_5		0x6094
#define MAC_ADDR_6		0x6098
#define MAC_ADDR_7		0x609c
#define MAC_ADDR_8		0x60a0
#define MAC_ADDR_FILTER_0	0x60a4
#define MAC_ADDR_FILTER_1	0x60a8
#define MAC_ADDR_FILTER_2	0x60ac
#define MAC_ADDR_FILTER_MASK21	0x60b0
#define MAC_ADDR_FILTER_MASK0	0x60b4
#define MAC_HASHTABLE		0x60c0
#define RANSEED			0x6130
#define MIFFRAME		0x620c
#define MIFCONFIG		0x6210
#define MIFINTMASK		0x6214
#define MIFSTATUS		0x6218
#define DATAPATHMODE		0x9050

/* -- 0x000C	R-C	Global Interrupt status. 
 * d: 0x00000000	bits 0-6 cleared on read (C)
 */
#define GMAC_IRQ_TX_INT_ME	0x00000001	/* C Frame with INT_ME bit set in fifo */
#define GMAC_IRQ_TX_ALL		0x00000002	/* C TX descriptor ring empty */
#define GMAC_IRQ_TX_DONE	0x00000004	/* C moved from host to TX fifo */
#define GMAC_IRQ_RX_DONE	0x00000010	/* C moved from RX fifo to host */
#define GMAC_IRQ_RX_NO_BUF	0x00000020	/* C No RX buffer available */
#define GMAC_IRQ_RX_TAG_ERR	0x00000040	/* C RX tag error */

#define GMAC_IRQ_PCS		0x00002000	/* PCS interrupt ? */
#define GMAC_IRQ_MAC_TX		0x00004000	/* MAC tx register set */
#define GMAC_IRQ_MAC_RX		0x00008000	/* MAC rx register set  */
#define GMAC_IRQ_MAC_CTRL	0x00010000	/* MAC control register set  */
#define GMAC_IRQ_MIF		0x00020000	/* MIF status register set */
#define GMAC_IRQ_BUS_ERROR	0x00040000	/* Bus error status register set */

#define GMAC_IRQ_TX_COMP	0xfff80000	/* TX completion mask */

/* -- 0x6210	RW	MIF config reg
 */

#define	GMAC_MIF_CFGPS			0x00000001	/* PHY Select */
#define	GMAC_MIF_CFGPE			0x00000002	/* Poll Enable */
#define	GMAC_MIF_CFGBB			0x00000004	/* Bit Bang Enable */
#define	GMAC_MIF_CFGPR_MASK		0x000000f8	/* Poll Register address */
#define	GMAC_MIF_CFGPR_SHIFT		3
#define	GMAC_MIF_CFGM0			0x00000100	/* MDIO_0 Data / MDIO_0 attached */
#define	GMAC_MIF_CFGM1			0x00000200	/* MDIO_1 Data / MDIO_1 attached */
#define	GMAC_MIF_CFGPD_MASK		0x00007c00	/* Poll Device PHY address */
#define	GMAC_MIF_CFGPD_SHIFT		10

#define	GMAC_MIF_POLL_DELAY		200

#define	GMAC_INTERNAL_PHYAD		1		/* PHY address for int. transceiver */
#define	GMAC_EXTERNAL_PHYAD		0		/* PHY address for ext. transceiver */


/* -- 0x6214	RW	MIF interrupt mask reg
 *			same as basic/status Register
 */

/* -- 0x6214	RW	MIF basic/status reg
 *			The Basic portion of this register indicates the last
 *			value of the register read indicated in the POLL REG field
 *			of the Configuration Register.
 *			The Status portion indicates bit(s) that have changed.
 *			The MIF Mask register is corresponding to this register in
 *			terms of the bit(s) that need to be masked for generating
 *			interrupt on the MIF Interrupt Bit of the Global Status Rgister.
 */

#define	GMAC_MIF_STATUS			0x0000ffff	/* 0-15 : Status */
#define	GMAC_MIF_BASIC			0xffff0000	/* 16-31 : Basic register */

