#ifndef i2oproc_h
#define i2oproc_h

/*
 * Fixme: make this dependent on architecture
 * The official header files to this already...but we can't use them
 */
#define     I2O_64BIT_CONTEXT          0

typedef struct _i2o_msg {
	u8 ver_offset;
	u8 msg_flags;
	u16 msg_size;
	u32 target_addr:12;
	u32 initiator_addr:12;
	u32 function:8;
	u32 init_context;	/* FIXME: 64-bit support! */
} i2o_msg, *pi2o_msg;

typedef struct _i2o_reply_message {
	i2o_msg msg_frame;
	u32 tctx;		/* FIXME: 64-bit */
	u16 detailed_status_code;
	u8 reserved;
	u8 req_status;
} i2o_reply_msg, *pi2o_reply_msg;

typedef struct _i2o_mult_reply_message {
	i2o_msg msg_frame;
	u32 tctx;		/* FIXME: 64-bit */
	u16 detailed_status_code;
	u8 reserved;
	u8 req_status;
} i2o_mult_reply_msg, *pi2o_mult_reply_msg;
   
#endif				/* i2oproc_h */
