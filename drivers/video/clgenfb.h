
/* definitions for Piccolo/SD64 VGA controller chip   */
/* these definitions might most of the time also work */
/* for other CL-GD542x/543x based boards..            */

/*** External/General Registers ***/
#define POS102	0x102  	/* POS102 register */
#define VSSM	0x46e8 	/* Adapter Sleep */
#define VSSM2	0x3c3	/* Motherboard Sleep */
#define MISC_W	0x3c2	/* Miscellaneous Output register, write */
#define MISC_R	0x3cc	/* Miscellaneous Output register, read */
#define FC_W	0x3da	/* Feature Control Register, write (color) */
#define FC_R	0x3ca	/* Feature Control Register, read */
#define FEAT	0x3c2	/* Input Status Register 0 */
#define STAT	0x3da	/* Input Status Register 1, read-only */
#define M_3C6	0x3c6	/* Pixel Mask */
#define M_3C7_W	0x3c7   /* Pixel Address Read Mode (write) */
#define M_3C7_R	0x3c7	/* DAC State (read-only */
#define M_3C8	0x3c8	/* Pixel Address Write Mode */
#define M_3C9	0x3c9	/* Pixel Data */

/*** VGA Sequencer Registers ***/
#define SEQRX	0x3c4	/* Sequencer Index */
#define SEQR0	0x0	/* Reset */
#define SEQR1     0x1	/* Clocking Mode */
#define SEQR2	0x2	/* Plane Mask / Write Pixel Extension */
#define SEQR3	0x3	/* Character Map Select */
#define SEQR4	0x4	/* Memory Mode */
/* the following are from the "extension registers" group */
#define SEQR6	0x6	/* Unlock ALL Extensions */
#define SEQR7	0x7	/* Extended Sequencer Mode */
#define SEQR8	0x8	/* EEPROM Control */
#define SEQR9	0x9	/* Scratch Pad 0 (do not access!) */
#define SEQRA	0xa	/* Scratch Pad 1 (do not access!) */
#define SEQRB	0xb	/* VCLK0 Numerator */
#define SEQRC	0xc	/* VCLK1 Numerator */
#define SEQRD	0xd	/* VCLK2 Numerator */
#define SEQRE	0xe	/* VCLK3 Numerator */
#define SEQRF	0xf	/* DRAM Control */
#define SEQR10	0x10	/* Graphics Cursor X Position */
#define SEQR11	0x11	/* Graphics Cursor Y Position */
#define SEQR12	0x12	/* Graphics Cursor Attributes */
#define SEQR13	0x13	/* Graphics Cursor Pattern Address Offset */
#define SEQR14	0x14	/* Scratch Pad 2 (CL-GD5426/'28 Only) (do not access!) */
#define SEQR15	0x15	/* Scratch Pad 3 (CL-GD5426/'28 Only) (do not access!) */
#define SEQR16	0x16	/* Performance Tuning (CL-GD5424/'26/'28 Only) */
#define SEQR17	0x17	/* Configuration ReadBack and Extended Control (CL-GF5428 Only) */
#define SEQR18	0x18	/* Signature Generator Control (Not CL-GD5420) */
#define SEQR19	0x19	/* Signature Generator Result Low Byte (Not CL-GD5420) */
#define SEQR1A	0x1a	/* Signature Generator Result High Byte (Not CL-GD5420) */
#define SEQR1B	0x1b	/* VCLK0 Denominator and Post-Scalar Value */
#define SEQR1C	0x1c	/* VCLK1 Denominator and Post-Scalar Value */
#define SEQR1D	0x1d	/* VCLK2 Denominator and Post-Scalar Value */
#define SEQR1E	0x1e	/* VCLK3 Denominator and Post-Scalar Value */
#define SEQR1F	0x1f	/* BIOS ROM write enable and MCLK Select */

/*** CRT Controller Registers ***/
#define CRTX	0x3d4	/* CRTC Index */
#define	CRT0	0x0	/* Horizontal Total */
#define CRT1	0x1	/* Horizontal Display End */
#define CRT2	0x2	/* Horizontal Blanking Start */
#define CRT3	0x3	/* Horizontal Blabking End */
#define CRT4	0x4	/* Horizontal Sync Start */
#define CRT5	0x5	/* Horizontal Sync End */
#define CRT6 	0x6	/* Vertical Total */
#define CRT7	0x7	/* Overflow */
#define CRT8	0x8	/* Screen A Preset Row Scan */
#define CRT9	0x9	/* Character Cell Height */
#define CRTA	0xa	/* Text Cursor Start */
#define CRTB	0xb	/* Text Cursor End */
#define CRTC	0xc	/* Screen Start Address High */
#define CRTD	0xd	/* Screen Start Address Low */
#define CRTE	0xe	/* Text Cursor Location High */
#define CRTF	0xf	/* Text Cursor Location Low */
#define CRT10	0x10	/* Vertical Sync Start */
#define CRT11	0x11	/* Vertical Sync End */
#define CRT12	0x12	/* Vertical Display End */
#define CRT13	0x13	/* Offset */
#define CRT14	0x14	/* Underline Row Scan */
#define CRT15  	0x15	/* Vertical Blanking Start */
#define CRT16	0x16	/* Vertical Blanking End */
#define CRT17	0x17	/* Mode Control */
#define CRT18	0x18	/* Line Compare */
#define CRT22	0x22	/* Graphics Data Latches ReadBack */
#define CRT24	0x24	/* Attribute Controller Toggle ReadBack */
#define CRT26	0x26	/* Attribute Controller Index ReadBack */
/* the following are from the "extension registers" group */
#define CRT19	0x19	/* Interlace End */
#define CRT1A	0x1a	/* Interlace Control */
#define CRT1B	0x1b	/* Extended Display Controls */
#define CRT1C	0x1c	/* Sync adjust and genlock register */
#define CRT1D	0x1d	/* Overlay Extended Control register */
#define CRT25	0x25	/* Part Status Register */
#define CRT27	0x27	/* ID Register */
#define CRT51	0x51	/* P4 disable "flicker fixer" */

/*** Graphics Controller Registers ***/
#define GRX	0x3ce	/* Graphics Controller Index */
#define GR0	0x0	/* Set/Reset, Write Mode 5 Background Extension */
#define GR1	0x1	/* Set/Reset Enable, Write Mode 4, 5 Foreground Ext. */
#define GR2	0x2	/* Color Compare */
#define GR3	0x3	/* Data Rotate */
#define GR4	0x4	/* Read Map Select */
#define GR5	0x5	/* Mode */
#define GR6	0x6	/* Miscellaneous */
#define GR7	0x7     /* Color Don't Care */
#define GR8	0x8	/* Bit Mask */
/* the following are from the "extension registers" group */
#define GR9	0x9	/* Offset Register 0 */
#define GRA	0xa	/* Offset Register 1 */
#define GRB	0xb	/* Graphics Controller Mode Extensions */
#define GRC	0xc	/* Color Key (CL-GD5424/'26/'28 Only) */
#define GRD	0xd	/* Color Key Mask (CL-GD5424/'26/'28 Only) */
#define GRE	0xe	/* Miscellaneous Control (Cl-GD5428 Only) */
#define GRF	0xf	/* Display Compression Control register */
#define GR10	0x10	/* 16-bit Pixel BG Color High Byte (Not CL-GD5420) */
#define GR11	0x11	/* 16-bit Pixel FG Color High Byte (Not CL-GD5420) */
#define GR12	0x12	/* Background Color Byte 2 Register */
#define GR13	0x13	/* Foreground Color Byte 2 Register */
#define GR14	0x14	/* Background Color Byte 3 Register */
#define GR15	0x15	/* Foreground Color Byte 3 Register */
/* the following are CL-GD5426/'28 specific blitter registers */
#define GR20	0x20	/* BLT Width Low */
#define GR21	0x21	/* BLT Width High */
#define GR22	0x22	/* BLT Height Low */
#define GR23	0x23	/* BLT Height High */
#define GR24	0x24	/* BLT Destination Pitch Low */
#define GR25	0x25	/* BLT Destination Pitch High */
#define GR26	0x26	/* BLT Source Pitch Low */
#define GR27	0x27	/* BLT Source Pitch High */
#define GR28	0x28	/* BLT Destination Start Low */
#define GR29	0x29	/* BLT Destination Start Mid */
#define GR2A    0x2a	/* BLT Destination Start High */
#define GR2C	0x2c	/* BLT Source Start Low */
#define GR2D	0x2d	/* BLT Source Start Mid */
#define GR2E	0x2e	/* BLT Source Start High */
#define GR2F	0x2f	/* Picasso IV Blitter compat mode..? */
#define GR30	0x30	/* BLT Mode */
#define GR31	0x31	/* BLT Start/Status */
#define GR32	0x32	/* BLT Raster Operation */
#define GR33	0x33	/* another P4 "compat" register.. */
#define GR34	0x34	/* Transparent Color Select Low */
#define GR35	0x35	/* Transparent Color Select High */
#define GR38	0x38	/* Source Transparent Color Mask Low */
#define GR39	0x39	/* Source Transparent Color Mask High */

/*** Attribute Controller Registers ***/
#define ARX	0x3c0	/* Attribute Controller Index */
#define AR0     0x0	/* Attribute Controller Palette Register 0 */
#define AR1     0x1	/* Attribute Controller Palette Register 1 */
#define AR2     0x2	/* Attribute Controller Palette Register 2 */
#define AR3     0x3	/* Attribute Controller Palette Register 3 */
#define AR4     0x4	/* Attribute Controller Palette Register 4 */
#define AR5     0x5	/* Attribute Controller Palette Register 5 */
#define AR6     0x6	/* Attribute Controller Palette Register 6 */
#define AR7     0x7	/* Attribute Controller Palette Register 7 */
#define AR8     0x8	/* Attribute Controller Palette Register 8 */
#define AR9     0x9	/* Attribute Controller Palette Register 9 */
#define ARA     0xa	/* Attribute Controller Palette Register 10 */
#define ARB     0xb	/* Attribute Controller Palette Register 11 */
#define ARC     0xc	/* Attribute Controller Palette Register 12 */
#define ARD     0xd	/* Attribute Controller Palette Register 13 */
#define ARE     0xe	/* Attribute Controller Palette Register 14 */
#define ARF     0xf	/* Attribute Controller Palette Register 15 */
#define AR10	0x10	/* Attribute Controller Mode Register */
#define AR11	0x11	/* Overscan (Border) Color Register */
#define AR12	0x12    /* Color Plane Enable Register */
#define AR13	0x13	/* Pixel Panning Register */
#define AR14	0x14	/* Color Select Register */
#define AR33	0x33	/* The "real" Pixel Panning register (?) */
#define AR34	0x34	/* *TEST* */

/*** Extension Registers ***/
#define HDR	0x3c6	/* Hidden DAC Register (Not CL-GD5420) */

