
#define PCILYNX_DRIVER_NAME      "pcilynx"
#define PCILYNX_MAJOR            177

#define PCILYNX_MINOR_AUX_START  0
#define PCILYNX_MINOR_ROM_START  16
#define PCILYNX_MINOR_RAM_START  32

#define PCILYNX_MAX_REGISTER     0xfff
#define PCILYNX_MAX_MEMORY       0xffff

#define PCI_DEVICE_ID_TI_PCILYNX 0x8000
#define MAX_PCILYNX_CARDS        4
#define LOCALRAM_SIZE            64

#define NUM_ISORCV_PCL           4
#define MAX_ISORCV_SIZE          2048
#define ISORCV_PER_PAGE          (PAGE_SIZE / MAX_ISORCV_SIZE)
#define ISORCV_PAGES             (NUM_ISORCV_PCL / ISORCV_PER_PAGE)

/* only iso rcv uses these definitions so far */
#define CHANNEL_LOCALBUS         0
#define CHANNEL_ASYNC_RCV        1
#define CHANNEL_ISO_RCV          2
#define CHANNEL_ASYNC_SEND       3

typedef int pcl_t;

struct ti_lynx {
        int id; /* sequential card number */

        spinlock_t lock;

        struct pci_dev *dev;

        struct {
                unsigned reg_1394a:1;
                u32 vendor;
                u32 product;
        } phyic;

        enum { clear, have_intr, have_aux_buf, have_pcl_mem,
               have_1394_buffers, have_iomappings } state;
        
        /* remapped memory spaces */
        void *registers;
        void *local_rom;
        void *local_ram;
        void *aux_port;


        atomic_t aux_intr_seen;
        wait_queue_head_t aux_intr_wait;

        void *mem_dma_buffer;
        struct semaphore mem_dma_mutex;
        wait_queue_head_t mem_dma_intr_wait;

        /*
         * use local RAM of LOCALRAM_SIZE (in kB) for PCLs, which allows for 
         * LOCALRAM_SIZE * 8 PCLs (each sized 128 bytes);
         * the following is an allocation bitmap 
         */
        u8 pcl_bmap[LOCALRAM_SIZE];

#ifndef CONFIG_IEEE1394_LYNXRAM
	/* point to PCLs memory area if needed */
	void *pcl_mem;
#endif

        /* PCLs for local mem / aux transfers */
        struct {
                pcl_t start, cmd, mod, max;
        } mem_pcl;

        /* IEEE-1394 part follows */
        struct hpsb_host *host;

        int phyid, isroot;

        spinlock_t phy_reg_lock;

        pcl_t rcv_pcl_start, rcv_pcl;
        void *rcv_page;
        int rcv_active;

        pcl_t async_pcl_start, async_pcl;
        struct hpsb_packet *async_queue;
        spinlock_t async_queue_lock;

        struct {
                pcl_t pcl[NUM_ISORCV_PCL];
                u32 stat[NUM_ISORCV_PCL];
                void *page[ISORCV_PAGES];
                pcl_t pcl_start;
                int chan_count;
                int next, last, used, running;
                struct tq_struct tq;
                spinlock_t lock;
        } iso_rcv;
};

/* the per-file data structure for mem space access */
struct memdata {
        struct ti_lynx *lynx;
        int cid;
        int aux_intr_last_seen;
        enum { rom, aux, ram } type;
};



/*
 * Register read and write helper functions.
 */
inline static void reg_write(const struct ti_lynx *lynx, int offset, u32 data)
{
        writel(data, lynx->registers + offset);
}

inline static u32 reg_read(const struct ti_lynx *lynx, int offset)
{
        return readl(lynx->registers + offset);
}

inline static void reg_set_bits(const struct ti_lynx *lynx, int offset,
                                u32 mask)
{
        reg_write(lynx, offset, (reg_read(lynx, offset) | mask));
}

inline static void reg_clear_bits(const struct ti_lynx *lynx, int offset,
                                  u32 mask)
{
        reg_write(lynx, offset, (reg_read(lynx, offset) & ~mask));
}



/* chip register definitions follow */

#define MISC_CONTROL                      0x40
#define MISC_CONTROL_SWRESET              (1<<0)

#define PCI_INT_STATUS                    0x48
#define PCI_INT_ENABLE                    0x4c               
/* status and enable have identical bit numbers */
#define PCI_INT_INT_PEND                  (1<<31)
#define PCI_INT_FORCED_INT                (1<<30)
#define PCI_INT_SLV_ADR_PERR              (1<<28)
#define PCI_INT_SLV_DAT_PERR              (1<<27)
#define PCI_INT_MST_DAT_PERR              (1<<26)
#define PCI_INT_MST_DEV_TIMEOUT           (1<<25)
#define PCI_INT_INTERNAL_SLV_TIMEOUT      (1<<23)
#define PCI_INT_AUX_TIMEOUT               (1<<18)
#define PCI_INT_AUX_INT                   (1<<17)
#define PCI_INT_1394                      (1<<16)
#define PCI_INT_DMA4_PCL                  (1<<9)
#define PCI_INT_DMA4_HLT                  (1<<8)
#define PCI_INT_DMA3_PCL                  (1<<7)
#define PCI_INT_DMA3_HLT                  (1<<6)
#define PCI_INT_DMA2_PCL                  (1<<5)
#define PCI_INT_DMA2_HLT                  (1<<4)
#define PCI_INT_DMA1_PCL                  (1<<3)
#define PCI_INT_DMA1_HLT                  (1<<2)
#define PCI_INT_DMA0_PCL                  (1<<1)
#define PCI_INT_DMA0_HLT                  (1<<0)
/* all DMA interrupts combined: */
#define PCI_INT_DMA_ALL                   0x3ff

#define PCI_INT_DMA_HLT(chan)             (1 << (chan * 2))
#define PCI_INT_DMA_PCL(chan)             (1 << (chan * 2 + 1))

#define LBUS_ADDR                         0xb4
#define LBUS_ADDR_SEL_RAM                 (0x0<<16)
#define LBUS_ADDR_SEL_ROM                 (0x1<<16)
#define LBUS_ADDR_SEL_AUX                 (0x2<<16)
#define LBUS_ADDR_SEL_ZV                  (0x3<<16)       

#define GPIO_CTRL_A                       0xb8
#define GPIO_CTRL_B                       0xbc
#define GPIO_DATA_BASE                    0xc0

#define DMA_BREG(base, chan)              (base + chan * 0x20)
#define DMA_SREG(base, chan)              (base + chan * 0x10)

#define DMA0_PREV_PCL                     0x100               
#define DMA1_PREV_PCL                     0x120
#define DMA2_PREV_PCL                     0x140
#define DMA3_PREV_PCL                     0x160
#define DMA4_PREV_PCL                     0x180
#define DMA_PREV_PCL(chan)                (DMA_BREG(DMA0_PREV_PCL, chan))

#define DMA0_CURRENT_PCL                  0x104            
#define DMA1_CURRENT_PCL                  0x124
#define DMA2_CURRENT_PCL                  0x144
#define DMA3_CURRENT_PCL                  0x164
#define DMA4_CURRENT_PCL                  0x184
#define DMA_CURRENT_PCL(chan)             (DMA_BREG(DMA0_CURRENT_PCL, chan))

#define DMA0_CHAN_STAT                    0x10c
#define DMA1_CHAN_STAT                    0x12c
#define DMA2_CHAN_STAT                    0x14c
#define DMA3_CHAN_STAT                    0x16c
#define DMA4_CHAN_STAT                    0x18c
#define DMA_CHAN_STAT(chan)               (DMA_BREG(DMA0_CHAN_STAT, chan))
/* CHAN_STATUS registers share bits */
#define DMA_CHAN_STAT_SELFID              (1<<31)
#define DMA_CHAN_STAT_ISOPKT              (1<<30)
#define DMA_CHAN_STAT_PCIERR              (1<<29)
#define DMA_CHAN_STAT_PKTERR              (1<<28)
#define DMA_CHAN_STAT_PKTCMPL             (1<<27)
#define DMA_CHAN_STAT_SPECIALACK          (1<<14)


#define DMA0_CHAN_CTRL                    0x110              
#define DMA1_CHAN_CTRL                    0x130
#define DMA2_CHAN_CTRL                    0x150
#define DMA3_CHAN_CTRL                    0x170
#define DMA4_CHAN_CTRL                    0x190
#define DMA_CHAN_CTRL(chan)               (DMA_BREG(DMA0_CHAN_CTRL, chan))
/* CHAN_CTRL registers share bits */
#define DMA_CHAN_CTRL_ENABLE              (1<<31)      
#define DMA_CHAN_CTRL_BUSY                (1<<30)
#define DMA_CHAN_CTRL_LINK                (1<<29)

#define DMA0_READY                        0x114
#define DMA1_READY                        0x134
#define DMA2_READY                        0x154
#define DMA3_READY                        0x174
#define DMA4_READY                        0x194
#define DMA_READY(chan)                   (DMA_BREG(DMA0_READY, chan))

#define DMA_GLOBAL_REGISTER               0x908

#define FIFO_SIZES                        0xa00

#define FIFO_CONTROL                      0xa10
#define GRF_FLUSH                         (1<<4)
#define ITF_FLUSH                         (1<<3)
#define ATF_FLUSH                         (1<<2)

#define FIFO_XMIT_THRESHOLD               0xa14

#define DMA0_WORD0_CMP_VALUE              0xb00
#define DMA1_WORD0_CMP_VALUE              0xb10
#define DMA2_WORD0_CMP_VALUE              0xb20
#define DMA3_WORD0_CMP_VALUE              0xb30
#define DMA4_WORD0_CMP_VALUE              0xb40
#define DMA_WORD0_CMP_VALUE(chan)         (DMA_SREG(DMA0_WORD0_CMP_VALUE, chan))

#define DMA0_WORD0_CMP_ENABLE             0xb04
#define DMA1_WORD0_CMP_ENABLE             0xb14
#define DMA2_WORD0_CMP_ENABLE             0xb24
#define DMA3_WORD0_CMP_ENABLE             0xb34
#define DMA4_WORD0_CMP_ENABLE             0xb44
#define DMA_WORD0_CMP_ENABLE(chan)        (DMA_SREG(DMA0_WORD0_CMP_ENABLE,chan))

#define DMA0_WORD1_CMP_VALUE              0xb08
#define DMA1_WORD1_CMP_VALUE              0xb18
#define DMA2_WORD1_CMP_VALUE              0xb28
#define DMA3_WORD1_CMP_VALUE              0xb38
#define DMA4_WORD1_CMP_VALUE              0xb48
#define DMA_WORD1_CMP_VALUE(chan)         (DMA_SREG(DMA0_WORD1_CMP_VALUE, chan))

#define DMA0_WORD1_CMP_ENABLE             0xb0c
#define DMA1_WORD1_CMP_ENABLE             0xb1c
#define DMA2_WORD1_CMP_ENABLE             0xb2c
#define DMA3_WORD1_CMP_ENABLE             0xb3c
#define DMA4_WORD1_CMP_ENABLE             0xb4c
#define DMA_WORD1_CMP_ENABLE(chan)        (DMA_SREG(DMA0_WORD1_CMP_ENABLE,chan))
/* word 1 compare enable flags */
#define DMA_WORD1_CMP_MATCH_OTHERBUS      (1<<15)
#define DMA_WORD1_CMP_MATCH_BROADCAST     (1<<14)
#define DMA_WORD1_CMP_MATCH_BUS_BCAST     (1<<13)
#define DMA_WORD1_CMP_MATCH_NODE_BCAST    (1<<12)
#define DMA_WORD1_CMP_MATCH_LOCAL         (1<<11)
#define DMA_WORD1_CMP_ENABLE_SELF_ID      (1<<10)
#define DMA_WORD1_CMP_ENABLE_MASTER       (1<<8)

#define LINK_ID                           0xf00
#define LINK_ID_BUS(id)                   (id<<22)
#define LINK_ID_NODE(id)                  (id<<16)

#define LINK_CONTROL                      0xf04
#define LINK_CONTROL_BUSY                 (1<<29)
#define LINK_CONTROL_TX_ISO_EN            (1<<26)
#define LINK_CONTROL_RX_ISO_EN            (1<<25)
#define LINK_CONTROL_TX_ASYNC_EN          (1<<24)
#define LINK_CONTROL_RX_ASYNC_EN          (1<<23)
#define LINK_CONTROL_RESET_TX             (1<<21)
#define LINK_CONTROL_RESET_RX             (1<<20)
#define LINK_CONTROL_CYCMASTER            (1<<11)
#define LINK_CONTROL_CYCSOURCE            (1<<10)
#define LINK_CONTROL_CYCTIMEREN           (1<<9)
#define LINK_CONTROL_RCV_CMP_VALID        (1<<7)
#define LINK_CONTROL_SNOOP_ENABLE         (1<<6)

#define CYCLE_TIMER                       0xf08

#define LINK_PHY                          0xf0c
#define LINK_PHY_READ                     (1<<31)
#define LINK_PHY_WRITE                    (1<<30)
#define LINK_PHY_ADDR(addr)               (addr<<24)
#define LINK_PHY_WDATA(data)              (data<<16)
#define LINK_PHY_RADDR(addr)              (addr<<8)


#define LINK_INT_STATUS                   0xf14
#define LINK_INT_ENABLE                   0xf18
/* status and enable have identical bit numbers */
#define LINK_INT_LINK_INT                 (1<<31)
#define LINK_INT_PHY_TIMEOUT              (1<<30)
#define LINK_INT_PHY_REG_RCVD             (1<<29)
#define LINK_INT_PHY_BUSRESET             (1<<28)
#define LINK_INT_TX_RDY                   (1<<26)
#define LINK_INT_RX_DATA_RDY              (1<<25)
#define LINK_INT_ISO_STUCK                (1<<20)
#define LINK_INT_ASYNC_STUCK              (1<<19)
#define LINK_INT_SENT_REJECT              (1<<17)
#define LINK_INT_HDR_ERR                  (1<<16)
#define LINK_INT_TX_INVALID_TC            (1<<15)
#define LINK_INT_CYC_SECOND               (1<<11)
#define LINK_INT_CYC_START                (1<<10)
#define LINK_INT_CYC_DONE                 (1<<9)
#define LINK_INT_CYC_PENDING              (1<<8)
#define LINK_INT_CYC_LOST                 (1<<7)
#define LINK_INT_CYC_ARB_FAILED           (1<<6)
#define LINK_INT_GRF_OVERFLOW             (1<<5)
#define LINK_INT_ITF_UNDERFLOW            (1<<4)
#define LINK_INT_ATF_UNDERFLOW            (1<<3)
#define LINK_INT_ISOARB_FAILED            (1<<0) 

/* PHY specifics */
#define PHY_VENDORID_TI                 0x800028
#define PHY_PRODUCTID_TSB41LV03         0x000000


/* this is the physical layout of a PCL, its size is 128 bytes */
struct ti_pcl {
        u32 next;
        u32 async_error_next;
        u32 user_data;
        u32 pcl_status;
        u32 remaining_transfer_count;
        u32 next_data_buffer;
        struct {
                u32 control;
                u32 pointer;
        } buffer[13] __attribute__ ((packed));
} __attribute__ ((packed));

#include <linux/stddef.h>
#define pcloffs(MEMBER) (offsetof(struct ti_pcl, MEMBER))


#ifdef CONFIG_IEEE1394_PCILYNX_LOCALRAM

inline static void put_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           const struct ti_pcl *pcl)
{
        int i;
        u32 *in = (u32 *)pcl;
        u32 *out = (u32 *)(lynx->local_ram + pclid * sizeof(struct ti_pcl));

        for (i = 0; i < 32; i++, out++, in++) {
                writel(cpu_to_le32(*in), out);
        }
}

inline static void get_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           struct ti_pcl *pcl)
{
        int i;
        u32 *out = (u32 *)pcl;
        u32 *in = (u32 *)(lynx->local_ram + pclid * sizeof(struct ti_pcl));

        for (i = 0; i < 32; i++, out++, in++) {
                *out = le32_to_cpu(readl(in));
        }
}

inline static u32 pcl_bus(const struct ti_lynx *lynx, pcl_t pclid)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
        return lynx->dev->base_address[1] + pclid * sizeof(struct ti_pcl);
#else
        return lynx->dev->resource[1].start + pclid * sizeof(struct ti_pcl);
#endif
}

#else /* CONFIG_IEEE1394_PCILYNX_LOCALRAM */

inline static void put_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           const struct ti_pcl *pcl)
{
        memcpy_le32((u32 *)(lynx->pcl_mem + pclid * sizeof(struct ti_pcl)),
                    (u32 *)pcl, sizeof(struct ti_pcl));
}

inline static void get_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           struct ti_pcl *pcl)
{
        memcpy_le32((u32 *)pcl,
                    (u32 *)(lynx->pcl_mem + pclid * sizeof(struct ti_pcl)),
                    sizeof(struct ti_pcl));
}

inline static u32 pcl_bus(const struct ti_lynx *lynx, pcl_t pclid)
{
        return virt_to_bus(lynx->pcl_mem) + pclid * sizeof(struct ti_pcl);
}

#endif /* CONFIG_IEEE1394_PCILYNX_LOCALRAM */


inline static void run_sub_pcl(const struct ti_lynx *lynx, pcl_t pclid, int idx,
                               int dmachan)
{
        reg_write(lynx, DMA0_CURRENT_PCL + dmachan * 0x20,
                  pcl_bus(lynx, pclid) + idx * 4);
        reg_write(lynx, DMA0_CHAN_CTRL + dmachan * 0x20,
                  DMA_CHAN_CTRL_ENABLE | DMA_CHAN_CTRL_LINK);
}

inline static void run_pcl(const struct ti_lynx *lynx, pcl_t pclid, int dmachan)
{
        run_sub_pcl(lynx, pclid, 0, dmachan);
}

#define PCL_NEXT_INVALID (1<<0)

/* transfer commands */
#define PCL_CMD_RCV            (0x1<<24)
#define PCL_CMD_RCV_AND_UPDATE (0xa<<24)
#define PCL_CMD_XMT            (0x2<<24)
#define PCL_CMD_UNFXMT         (0xc<<24)
#define PCL_CMD_PCI_TO_LBUS    (0x8<<24)
#define PCL_CMD_LBUS_TO_PCI    (0x9<<24)

/* aux commands */
#define PCL_CMD_NOP            (0x0<<24)
#define PCL_CMD_LOAD           (0x3<<24)
#define PCL_CMD_STOREQ         (0x4<<24)
#define PCL_CMD_STORED         (0xb<<24)
#define PCL_CMD_STORE0         (0x5<<24)
#define PCL_CMD_STORE1         (0x6<<24)
#define PCL_CMD_COMPARE        (0xe<<24)
#define PCL_CMD_SWAP_COMPARE   (0xf<<24)
#define PCL_CMD_ADD            (0xd<<24)
#define PCL_CMD_BRANCH         (0x7<<24)

/* BRANCH condition codes */
#define PCL_COND_DMARDY_SET    (0x1<<20)
#define PCL_COND_DMARDY_CLEAR  (0x2<<20)

#define PCL_GEN_INTR           (1<<19)
#define PCL_LAST_BUFF          (1<<18)
#define PCL_LAST_CMD           (PCL_LAST_BUFF)
#define PCL_WAITSTAT           (1<<17)
#define PCL_BIGENDIAN          (1<<16)


#define _(x) (__constant_cpu_to_be32(x))

quadlet_t lynx_csr_rom[] = {
        /* bus info block */
        _(0x04040000), /* info/CRC length, CRC */
        _(0x31333934), /* 1394 magic number */
        _(0xf064a000), /* misc. settings */
        _(0x08002850), /* vendor ID, chip ID high */
        _(0x0000ffff), /* chip ID low */
        /* root directory */
        _(0x00090000), /* CRC length, CRC */
        _(0x03080028), /* vendor ID (Texas Instr.) */
        _(0x81000009), /* offset to textual ID */
        _(0x0c000200), /* node capabilities */
        _(0x8d00000e), /* offset to unique ID */
        _(0xc7000010), /* offset to module independent info */
        _(0x04000000), /* module hardware version */
        _(0x81000026), /* offset to textual ID */
        _(0x09000000), /* node hardware version */
        _(0x81000026), /* offset to textual ID */
        /* module vendor ID textual */
        _(0x00080000), /* CRC length, CRC */
        _(0x00000000),
        _(0x00000000),
        _(0x54455841), /* "Texas Instruments" */
        _(0x5320494e),
        _(0x53545255),
        _(0x4d454e54),
        _(0x53000000),
        /* node unique ID leaf */
        _(0x00020000), /* CRC length, CRC */
        _(0x08002850), /* vendor ID, chip ID high */
        _(0x0000ffff), /* chip ID low */
        /* module dependent info */
        _(0x00060000), /* CRC length, CRC */
        _(0xb8000006), /* offset to module textual ID */
        _(0x81000004), /* ??? textual descriptor */
        _(0x39010000), /* SRAM size */
        _(0x3a010000), /* AUXRAM size */
        _(0x3b000000), /* AUX device */
        /* module textual ID */
        _(0x00050000), /* CRC length, CRC */
        _(0x00000000),
        _(0x00000000),
        _(0x54534231), /* "TSB12LV21" */
        _(0x324c5632),
        _(0x31000000),
        /* part number */
        _(0x00060000), /* CRC length, CRC */
        _(0x00000000),
        _(0x00000000),
        _(0x39383036), /* "9806000-0001" */
        _(0x3030342d),
        _(0x30303431),
        _(0x20000001),
        /* module hardware version textual */
        _(0x00050000), /* CRC length, CRC */
        _(0x00000000),
        _(0x00000000),
        _(0x5453424b), /* "TSBKPCITST" */
        _(0x50434954),
        _(0x53540000),
        /* node hardware version textual */
        _(0x00050000), /* CRC length, CRC */
        _(0x00000000),
        _(0x00000000),
        _(0x54534232), /* "TSB21LV03" */
        _(0x313c5630),
        _(0x33000000)
};

#undef _
