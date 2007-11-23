
#ifndef _OHCI1394_H
#define _OHCI1394_H

#include "ieee1394_types.h"

#define OHCI1394_DRIVER_NAME      "ohci1394"

#ifndef PCI_DEVICE_ID_TI_OHCI1394_LV22
#define PCI_DEVICE_ID_TI_OHCI1394_LV22 0x8009
#endif

#ifndef PCI_DEVICE_ID_TI_OHCI1394_LV23
#define PCI_DEVICE_ID_TI_OHCI1394_LV23 0x8019
#endif

#ifndef PCI_DEVICE_ID_TI_OHCI1394_LV26
#define PCI_DEVICE_ID_TI_OHCI1394_LV26 0x8020
#endif

#ifndef PCI_DEVICE_ID_VIA_OHCI1394
#define PCI_DEVICE_ID_VIA_OHCI1394 0x3044
#endif

#ifndef PCI_VENDOR_ID_SONY
#define PCI_VENDOR_ID_SONY 0x104d
#endif

#ifndef PCI_DEVICE_ID_SONY_CXD3222
#define PCI_DEVICE_ID_SONY_CXD3222 0x8039
#endif

#ifndef PCI_DEVICE_ID_NEC_1394
#define PCI_DEVICE_ID_NEC_1394 0x00cd
#endif

#ifndef PCI_DEVICE_ID_NEC_UPD72862
#define PCI_DEVICE_ID_NEC_UPD72862      0x0063
#endif

#ifndef PCI_DEVICE_ID_NEC_UPD72870
#define PCI_DEVICE_ID_NEC_UPD72870      0x00cd
#endif

#ifndef PCI_DEVICE_ID_NEC_UPD72871
#define PCI_DEVICE_ID_NEC_UPD72871      0x00ce
#endif

#define MAX_OHCI1394_CARDS        4

#define OHCI1394_MAX_AT_REQ_RETRIES       0x2
#define OHCI1394_MAX_AT_RESP_RETRIES      0x2
#define OHCI1394_MAX_PHYS_RESP_RETRIES    0x8

#define AR_REQ_NUM_DESC                   4 /* number of AR req descriptors */
#define AR_REQ_BUF_SIZE                4096 /* size of AR req buffers */
#define AR_REQ_SPLIT_BUF_SIZE          4096 /* split packet buffer */

#define AR_RESP_NUM_DESC                  4 /* number of AR resp descriptors */
#define AR_RESP_BUF_SIZE               4096 /* size of AR resp buffers */
#define AR_RESP_SPLIT_BUF_SIZE         4096 /* split packet buffer */

#define IR_NUM_DESC                      16 /* number of IR descriptors */
#define IR_BUF_SIZE                    6480 /* 6480 bytes/buffer */
#define IR_SPLIT_BUF_SIZE              8192 /* split packet buffer */

#define AT_REQ_NUM_DESC                  32 /* number of AT req descriptors */
#define AT_RESP_NUM_DESC                 32 /* number of AT resp descriptors */

struct dma_cmd {
        u32 control;
        u32 address;
        u32 branchAddress;
        u32 status;
};

struct at_dma_prg {
	struct dma_cmd begin;
	quadlet_t data[4];
	struct dma_cmd end;
};

/* DMA receive context */
struct dma_rcv_ctx {
	void *ohci;
	int ctx;
	unsigned int num_desc;
	unsigned int buf_size;
	unsigned int split_buf_size;
        struct dma_cmd **prg;
        quadlet_t **buf;
        unsigned int buf_ind;
        unsigned int buf_offset;
        quadlet_t *spb;
        spinlock_t lock;
        struct tq_struct task;
	int ctrlClear;
	int ctrlSet;
	int cmdPtr;
};

/* DMA transmit context */	
struct dma_trm_ctx {
	void *ohci;
	int ctx;
	unsigned int num_desc;
        struct at_dma_prg *prg;
        unsigned int prg_ind;
        unsigned int sent_ind;
	int free_prgs;
        quadlet_t *branchAddrPtr;
        struct hpsb_packet *first;
        struct hpsb_packet *last;
        spinlock_t lock;
        struct tq_struct task;
	int ctrlClear;
	int ctrlSet;
	int cmdPtr;
};

struct ti_ohci {
        int id; /* sequential card number */

        struct pci_dev *dev;

        u32 state;
        
        /* remapped memory spaces */
        void *registers; 

        quadlet_t *self_id_buffer; /* dma buffer for self-id packets */
        quadlet_t *csr_config_rom; /* buffer for csr config rom */

	unsigned int max_packet_size;

        /* async receive */
	struct dma_rcv_ctx *ar_resp_context;
	struct dma_rcv_ctx *ar_req_context;

	/* async transmit */
	struct dma_trm_ctx *at_resp_context;
	struct dma_trm_ctx *at_req_context;

        /* iso receive */
	struct dma_rcv_ctx *ir_context;
        u64 IR_channel_usage;
        spinlock_t IR_channel_lock;
	int nb_iso_ctx;

        /* IEEE-1394 part follows */
        struct hpsb_host *host;

        int phyid, isroot;

        spinlock_t phy_reg_lock;

        int NumBusResets;
};

/*
 * Register read and write helper functions.
 */
inline static void reg_write(const struct ti_ohci *ohci, int offset, u32 data)
{
        writel(data, ohci->registers + offset);
}

inline static u32 reg_read(const struct ti_ohci *ohci, int offset)
{
        return readl(ohci->registers + offset);
}

/* This structure is not properly initialized ... it is taken from
   the lynx_csr_rom written by Andreas ... Some fields in the root
   directory and the module dependent info needs to be modified
   I do not have the proper doc */
quadlet_t ohci_csr_rom[] = {
        /* bus info block */
        0x04040000, /* info/CRC length, CRC */
        0x31333934, /* 1394 magic number */
        0xf07da002, /* cyc_clk_acc = 125us, max_rec = 1024 */
        0x00000000, /* vendor ID, chip ID high (written from card info) */
        0x00000000, /* chip ID low (written from card info) */
        /* root directory - FIXME */
        0x00090000, /* CRC length, CRC */
        0x03080028, /* vendor ID (Texas Instr.) */
        0x81000009, /* offset to textual ID */
        0x0c000200, /* node capabilities */
        0x8d00000e, /* offset to unique ID */
        0xc7000010, /* offset to module independent info */
        0x04000000, /* module hardware version */
        0x81000026, /* offset to textual ID */
        0x09000000, /* node hardware version */
        0x81000026, /* offset to textual ID */
        /* module vendor ID textual */
        0x00080000, /* CRC length, CRC */
        0x00000000,
        0x00000000,
        0x54455841, /* "Texas Instruments" */
        0x5320494e,
        0x53545255,
        0x4d454e54,
        0x53000000,
        /* node unique ID leaf */
        0x00020000, /* CRC length, CRC */
        0x08002856, /* vendor ID, chip ID high */
        0x0000083E, /* chip ID low */
        /* module dependent info - FIXME */
        0x00060000, /* CRC length, CRC */
        0xb8000006, /* ??? offset to module textual ID */
        0x81000004, /* ??? textual descriptor */
        0x00000000, /* SRAM size */
        0x00000000, /* AUXRAM size */
        0x00000000, /* AUX device */
        /* module textual ID */
        0x00050000, /* CRC length, CRC */
        0x00000000,
        0x00000000,
        0x54534231, /* "TSB12LV22" */
        0x324c5632,
        0x32000000,
        /* part number */
        0x00060000, /* CRC length, CRC */
        0x00000000,
        0x00000000,
        0x39383036, /* "9806000-0001" */
        0x3030342d,
        0x30303431,
        0x20000001,
        /* module hardware version textual */
        0x00050000, /* CRC length, CRC */
        0x00000000,
        0x00000000,
        0x5453424b, /* "TSBKOHCI403" */
        0x4f484349,
        0x34303300,
        /* node hardware version textual */
        0x00050000, /* CRC length, CRC */
        0x00000000,
        0x00000000,
        0x54534234, /* "TSB41LV03" */
        0x314c5630,
        0x33000000
};


/* 2 KiloBytes of register space */
#define OHCI1394_REGISTER_SIZE                0x800       

/* register map */
#define OHCI1394_Version                      0x000
#define OHCI1394_GUID_ROM                     0x004
#define OHCI1394_ATRetries                    0x008
#define OHCI1394_CSRReadData                  0x00C
#define OHCI1394_CSRCompareData               0x010
#define OHCI1394_CSRControl                   0x014
#define OHCI1394_ConfigROMhdr                 0x018
#define OHCI1394_BusID                        0x01C
#define OHCI1394_BusOptions                   0x020
#define OHCI1394_GUIDHi                       0x024
#define OHCI1394_GUIDLo                       0x028
#define OHCI1394_ConfigROMmap                 0x034
#define OHCI1394_PostedWriteAddressLo         0x038
#define OHCI1394_PostedWriteAddressHi         0x03C
#define OHCI1394_VendorID                     0x040
#define OHCI1394_HCControlSet                 0x050
#define OHCI1394_HCControlClear               0x054
#define OHCI1394_SelfIDBuffer                 0x064
#define OHCI1394_SelfIDCount                  0x068
#define OHCI1394_IRMultiChanMaskHiSet         0x070
#define OHCI1394_IRMultiChanMaskHiClear       0x074
#define OHCI1394_IRMultiChanMaskLoSet         0x078
#define OHCI1394_IRMultiChanMaskLoClear       0x07C
#define OHCI1394_IntEventSet                  0x080
#define OHCI1394_IntEventClear                0x084
#define OHCI1394_IntMaskSet                   0x088
#define OHCI1394_IntMaskClear                 0x08C
#define OHCI1394_IsoXmitIntEventSet           0x090
#define OHCI1394_IsoXmitIntEventClear         0x094
#define OHCI1394_IsoXmitIntMaskSet            0x098
#define OHCI1394_IsoXmitIntMaskClear          0x09C
#define OHCI1394_IsoRecvIntEventSet           0x0A0
#define OHCI1394_IsoRecvIntEventClear         0x0A4
#define OHCI1394_IsoRecvIntMaskSet            0x0A8
#define OHCI1394_IsoRecvIntMaskClear          0x0AC
#define OHCI1394_FairnessControl              0x0DC
#define OHCI1394_LinkControlSet               0x0E0
#define OHCI1394_LinkControlClear             0x0E4
#define OHCI1394_NodeID                       0x0E8
#define OHCI1394_PhyControl                   0x0EC
#define OHCI1394_IsochronousCycleTimer        0x0F0
#define OHCI1394_AsReqFilterHiSet             0x100
#define OHCI1394_AsReqFilterHiClear           0x104
#define OHCI1394_AsReqFilterLoSet             0x108
#define OHCI1394_AsReqFilterLoClear           0x10C
#define OHCI1394_PhyReqFilterHiSet            0x110
#define OHCI1394_PhyReqFilterHiClear          0x114
#define OHCI1394_PhyReqFilterLoSet            0x118
#define OHCI1394_PhyReqFilterLoClear          0x11C
#define OHCI1394_PhyUpperBound                0x120
#define OHCI1394_AsReqTrContextControlSet     0x180
#define OHCI1394_AsReqTrContextControlClear   0x184
#define OHCI1394_AsReqTrCommandPtr            0x18C
#define OHCI1394_AsRspTrContextControlSet     0x1A0
#define OHCI1394_AsRspTrContextControlClear   0x1A4
#define OHCI1394_AsRspTrCommandPtr            0x1AC
#define OHCI1394_AsReqRcvContextControlSet    0x1C0
#define OHCI1394_AsReqRcvContextControlClear  0x1C4
#define OHCI1394_AsReqRcvCommandPtr           0x1CC
#define OHCI1394_AsRspRcvContextControlSet    0x1E0
#define OHCI1394_AsRspRcvContextControlClear  0x1E4
#define OHCI1394_AsRspRcvCommandPtr           0x1EC

/* Isochronous receive registers */
/* Add (32 * n) for context n */
#define OHCI1394_IrRcvContextControlSet       0x400
#define OHCI1394_IrRcvContextControlClear     0x404
#define OHCI1394_IrRcvCommandPtr              0x40C
#define OHCI1394_IrRcvContextMatch            0x410

/* Interrupts Mask/Events */

#define OHCI1394_reqTxComplete           0x00000001
#define OHCI1394_respTxComplete          0x00000002
#define OHCI1394_ARRQ                    0x00000004
#define OHCI1394_ARRS                    0x00000008
#define OHCI1394_RQPkt                   0x00000010
#define OHCI1394_RSPkt                   0x00000020
#define OHCI1394_isochTx                 0x00000040
#define OHCI1394_isochRx                 0x00000080
#define OHCI1394_postedWriteErr          0x00001000
#define OHCI1394_lockRespErr             0x00002000
#define OHCI1394_selfIDComplete          0x00010000
#define OHCI1394_busReset                0x00020000
#define OHCI1394_phy                     0x00080000
#define OHCI1394_cycleSynch              0x00100000
#define OHCI1394_cycle64Seconds          0x00200000
#define OHCI1394_cycleLost               0x00400000
#define OHCI1394_cycleInconsistent       0x00800000
#define OHCI1394_unrecoverableError      0x01000000
#define OHCI1394_cycleTooLong            0x02000000
#define OHCI1394_phyRegRcvd              0x04000000
#define OHCI1394_masterIntEnable         0x80000000

#define OUTPUT_MORE                      0x00000000
#define OUTPUT_MORE_IMMEDIATE            0x02000000
#define OUTPUT_LAST                      0x103c0000
#define OUTPUT_LAST_IMMEDIATE            0x123c0000

#define DMA_SPEED_100                    0x0
#define DMA_SPEED_200                    0x1
#define DMA_SPEED_400                    0x2

#endif

