/*****************************************************************
 *
 *  defines for 3Com Etherlink Plus adapter
 *
 *****************************************************************/

/*
 * I/O register offsets
 */
#define	PORT_COMMAND	0x00	/* read/write */
#define	PORT_STATUS	0x02	/* read only */
#define	PORT_AUXDMA	0x02	/* write only */
#define	PORT_DATA	0x04	/* read/write */
#define	PORT_CONTROL	0x06	/* read/write */

/*
 * host control registers bits
 */
#define	CONTROL_ATTN	0x80	/* attention */
#define	CONTROL_FLSH	0x40	/* flush data register */
#define CONTROL_DMAE	0x20	/* DMA enable */
#define CONTROL_DIR	0x10	/* direction */
#define	CONTROL_TCEN	0x08	/* terminal count interrupt enable */
#define	CONTROL_CMDE	0x04	/* command register interrupt enable */
#define	CONTROL_HSF2	0x02	/* host status flag 2 */
#define	CONTROL_HSF1	0x01	/* host status flag 1 */

/*
 * combinations of HSF flags used for PCB transmission
 */
#define	HSF_PCB_ACK	(CONTROL_HSF1)
#define	HSF_PCB_NAK	(CONTROL_HSF2)
#define	HSF_PCB_END	(CONTROL_HSF2|CONTROL_HSF1)
#define	HSF_PCB_MASK	(CONTROL_HSF2|CONTROL_HSF1)

/*
 * host status register bits
 */
#define	STATUS_HRDY	0x80	/* data register ready */
#define	STATUS_HCRE	0x40	/* command register empty */
#define	STATUS_ACRF	0x20	/* adapter command register full */
#define	STATUS_DIR 	0x10	/* direction */
#define	STATUS_DONE	0x08	/* DMA done */
#define	STATUS_ASF3	0x04	/* adapter status flag 3 */
#define	STATUS_ASF2	0x02	/* adapter status flag 2 */
#define	STATUS_ASF1	0x01	/* adapter status flag 1 */

/*
 * combinations of ASF flags used for PCB reception
 */
#define	ASF_PCB_ACK	(STATUS_ASF1)
#define	ASF_PCB_NAK	(STATUS_ASF2)
#define	ASF_PCB_END	(STATUS_ASF2|STATUS_ASF1)
#define	ASF_PCB_MASK	(STATUS_ASF2|STATUS_ASF1)

/*
 * host aux DMA register bits
 */
#define	AUXDMA_BRST	0x01	/* DMA burst */

/*
 * maximum amount of data data allowed in a PCB
 */
#define	MAX_PCB_DATA	62

/*****************************************************************
 *
 *  timeout value
 *	this is a rough value used for loops to stop them from 
 *	locking up the whole machine in the case of failure or
 *	error conditions
 *
 *****************************************************************/

#define	TIMEOUT	300

/*****************************************************************
 *
 * PCB commands
 *
 *****************************************************************/

enum {
  /*
   * host PCB commands
   */
  CMD_CONFIGURE_ADAPTER_MEMORY	= 0x01,
  CMD_CONFIGURE_82586		= 0x02,
  CMD_STATION_ADDRESS		= 0x03,
  CMD_DMA_DOWNLOAD		= 0x04,
  CMD_DMA_UPLOAD		= 0x05,
  CMD_PIO_DOWNLOAD		= 0x06,
  CMD_PIO_UPLOAD		= 0x07,
  CMD_RECEIVE_PACKET		= 0x08,
  CMD_TRANSMIT_PACKET		= 0x09,
  CMD_NETWORK_STATISTICS	= 0x0a,
  CMD_LOAD_MULTICAST_LIST	= 0x0b,
  CMD_CLEAR_PROGRAM		= 0x0c,
  CMD_DOWNLOAD_PROGRAM		= 0x0d,
  CMD_EXECUTE_PROGRAM		= 0x0e,
  CMD_SELF_TEST			= 0x0f,
  CMD_SET_STATION_ADDRESS	= 0x10,
  CMD_ADAPTER_INFO		= 0x11,
  NUM_TRANSMIT_CMDS,

  /*
   * adapter PCB commands
   */
  CMD_CONFIGURE_ADAPTER_RESPONSE	= 0x31,
  CMD_CONFIGURE_82586_RESPONSE		= 0x32,
  CMD_ADDRESS_RESPONSE			= 0x33,
  CMD_DOWNLOAD_DATA_REQUEST		= 0x34,
  CMD_UPLOAD_DATA_REQUEST		= 0x35,
  CMD_RECEIVE_PACKET_COMPLETE		= 0x38,
  CMD_TRANSMIT_PACKET_COMPLETE		= 0x39,
  CMD_NETWORK_STATISTICS_RESPONSE	= 0x3a,
  CMD_LOAD_MULTICAST_RESPONSE		= 0x3b,
  CMD_CLEAR_PROGRAM_RESPONSE		= 0x3c,
  CMD_DOWNLOAD_PROGRAM_RESPONSE		= 0x3d,
  CMD_EXECUTE_RESPONSE			= 0x3e,
  CMD_SELF_TEST_RESPONSE		= 0x3f,
  CMD_SET_ADDRESS_RESPONSE		= 0x40,
  CMD_ADAPTER_INFO_RESPONSE		= 0x41
};

