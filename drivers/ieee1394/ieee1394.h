/*
 * Generic IEEE 1394 definitions
 */

#ifndef _IEEE1394_IEEE1394_H
#define _IEEE1394_IEEE1394_H

#define TCODE_WRITEQ             0x0
#define TCODE_WRITEB             0x1
#define TCODE_WRITE_RESPONSE     0x2
#define TCODE_READQ              0x4
#define TCODE_READB              0x5
#define TCODE_READQ_RESPONSE     0x6
#define TCODE_READB_RESPONSE     0x7
#define TCODE_CYCLE_START        0x8
#define TCODE_LOCK_REQUEST       0x9
#define TCODE_ISO_DATA           0xa
#define TCODE_LOCK_RESPONSE      0xb

#define RCODE_COMPLETE           0x0
#define RCODE_CONFLICT_ERROR     0x4
#define RCODE_DATA_ERROR         0x5
#define RCODE_TYPE_ERROR         0x6
#define RCODE_ADDRESS_ERROR      0x7

#define EXTCODE_MASK_SWAP        0x1
#define EXTCODE_COMPARE_SWAP     0x2
#define EXTCODE_FETCH_ADD        0x3
#define EXTCODE_LITTLE_ADD       0x4
#define EXTCODE_BOUNDED_ADD      0x5
#define EXTCODE_WRAP_ADD         0x6

#define ACK_COMPLETE             0x1
#define ACK_PENDING              0x2
#define ACK_BUSY_X               0x4
#define ACK_BUSY_A               0x5
#define ACK_BUSY_B               0x6
#define ACK_DATA_ERROR           0xd
#define ACK_TYPE_ERROR           0xe 

/* Non-standard "ACK codes" for internal use */
#define ACKX_NONE                -1
#define ACKX_SEND_ERROR          -2
#define ACKX_ABORTED             -3
#define ACKX_TIMEOUT             -4


#define SPEED_100                0x0
#define SPEED_200                0x1
#define SPEED_400                0x2 

#define SELFID_PWRCL_NO_POWER    0x0
#define SELFID_PWRCL_PROVIDE_15W 0x1
#define SELFID_PWRCL_PROVIDE_30W 0x2
#define SELFID_PWRCL_PROVIDE_45W 0x3
#define SELFID_PWRCL_USE_1W      0x4
#define SELFID_PWRCL_USE_3W      0x5
#define SELFID_PWRCL_USE_6W      0x6
#define SELFID_PWRCL_USE_10W     0x7

#define SELFID_PORT_CHILD        0x3
#define SELFID_PORT_PARENT       0x2
#define SELFID_PORT_NCONN        0x1
#define SELFID_PORT_NONE         0x0   

#endif /* _IEEE1394_IEEE1394_H */
