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

/**************************************************************************
 * HRT related constants and structures
 **************************************************************************/
#define    I2O_BUS_LOCAL                               0
#define    I2O_BUS_ISA                                 1
#define    I2O_BUS_EISA                                2
#define    I2O_BUS_MCA                                 3
#define    I2O_BUS_PCI                                 4
#define    I2O_BUS_PCMCIA                              5
#define    I2O_BUS_NUBUS                               6
#define    I2O_BUS_CARDBUS                             7
#define    I2O_BUS_UNKNOWN                            0x80

typedef struct _i2o_pci_bus {
	u8 PciFunctionNumber;
	u8 PciDeviceNumber;
	u8 PciBusNumber;
	u8 reserved;
	u16 PciVendorID;
	u16 PciDeviceID;
} i2o_pci_bus, *pi2o_pci_bus;

typedef struct _i2o_local_bus {
	u16 LbBaseIOPort;
	u16 reserved;
	u32 LbBaseMemoryAddress;
} i2o_local_bus, *pi2o_local_bus;

typedef struct _i2o_isa_bus {
	u16 IsaBaseIOPort;
	u8 CSN;
	u8 reserved;
	u32 IsaBaseMemoryAddress;
} i2o_isa_bus, *pi2o_isa_bus;

/* I2O_EISA_BUS_INFO  */
typedef struct _i2o_eisa_bus_info {
	u16 EisaBaseIOPort;
	u8 reserved;
	u8 EisaSlotNumber;
	u32 EisaBaseMemoryAddress;
} i2o_eisa_bus, *pi2o_eisa_bus;

typedef struct _i2o_mca_bus {
	u16 McaBaseIOPort;
	u8 reserved;
	u8 McaSlotNumber;
	u32 McaBaseMemoryAddress;
} i2o_mca_bus, *pi2o_mca_bus;

typedef struct _i2o_other_bus {
	u16 BaseIOPort;
	u16 reserved;
	u32 BaseMemoryAddress;
} i2o_other_bus, *pi2o_other_bus;


typedef struct _i2o_hrt_entry {
	u32 adapter_id;
	u32 parent_tid:12;
	u32 state:4;
	u32 bus_num:8;
	u32 bus_type:8;
	union {
		i2o_pci_bus pci_bus;
		i2o_local_bus local_bus;
		i2o_isa_bus isa_bus;
		i2o_eisa_bus eisa_bus;
		i2o_mca_bus mca_bus;
		i2o_other_bus other_bus;
	} bus;
} i2o_hrt_entry, *pi2o_hrt_entry;

typedef struct _i2o_hrt {
	u16 num_entries;
	u8 entry_len;
	u8 hrt_version;
	u32 change_ind;
	i2o_hrt_entry hrt_entry[1];
} i2o_hrt, *pi2o_hrt;

typedef struct _i2o_lct_entry {
	u32 entry_size:16;
	u32 tid:12;
	u32 reserved:4;
	u32 change_ind;
	u32 device_flags;
	u32 class_id;
	u32 sub_class;
	u32 user_tid:12;
	u32 parent_tid:12;
	u32 bios_info:8;
	u8 identity_tag[8];
	u32 event_capabilities;
} i2o_lct_entry, *pi2o_lct_entry;

typedef struct _i2o_lct {
	u32 table_size:16;
	u32 boot_tid:12;
	u32 lct_ver:4;
	u32 iop_flags;
	u32 current_change_ind;
	i2o_lct_entry lct_entry[1];
} i2o_lct, *pi2o_lct;

#endif				/* i2oproc_h */
