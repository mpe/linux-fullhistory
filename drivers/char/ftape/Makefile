#
# Makefile for the ftape device driver.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

#  Valid ftape options are:
#       NO_TRACE        - if defined, only information and errors show up.
#       NO_TRACE_AT_ALL - if defined, no trace output shows up.
#       GCC_2_4_5_BUG   - must be set if using gcc-2.4.5 to prevent
#                         bad assembler-code for the dma handling.
#	NR_BUFFERS	- Number of ftape DMA buffers (keep it at 3!)
#       VERIFY_HEADERS  - if set the headers segments are verified after
#                         being written.
#       PROBE_FC10      - if defined will look for a FC-10 card at specified
#                         settings (FDC_BASE,FDC_IRQ,FDC_DMA) before using
#                         the standard fd controller.
#       FDC_BASE        - sets base address (only!) if using non-standard fdc
#       FDC_IRQ         - sets interrupt if FDC_BASE is defined
#       FDC_DMA         - sets dma channel if FDC_BASE is defined
#       MACH2           - Support for Mountain MACH-2 controller at either 1E0
#                         or 3E0, don't forget the FDC_OPT's !
#	CLK_48MHZ	- Set to 1. If you have a i82078-1 FDC and it does not
#			  work, try setting it to 0. (only used for i82078-1's)
#	FDC_82078SL	- If you have a 82078SL, define this.

FTAPE_OPT       = -DVERIFY_HEADERS -DNR_BUFFERS=3 -DCLK_48MHZ=1 \
	-DNO_TRACE -DFDC_82078SL

#  If you're using a non-standard floppy disk controller for the
#  tape drive, enable one (only!) of the following lines and set
#  the FDC_BASE, FDC_IRQ and FDC_DMA parameters to the actual values.
#
#  Note1: A FC-10/FC-20 controller must use either of DMA 1, 2, or 3.
#         DMA 5 and 7 does NOT work!.
#
#  Note2: IRQ 2 and IRQ 9 can be considered the same. When using IRQ 2
#         on a controller you must specify IRQ 9 here!
#
# For a Mountain MACH-2 controller, try 
#FDC_OPT         = -DMACH2 -DFDC_BASE=0x1E0 -DFDC_IRQ=6 -DFDC_DMA=2
#
# For Colorado CMS FC-10 or FC-20 controllers:
#FDC_OPT         = -DPROBE_FC10 -DFDC_BASE=0x180 -DFDC_IRQ=9 -DFDC_DMA=3
#
# Secondary floppy disk controller:
#FDC_OPT         = -DFDC_BASE=0x370 -DFDC_IRQ=9 -DFDC_DMA=3
#
# This enables some (most?) 2Mbps controllers:
#FDC_OPT         = -DFDC_BASE=0x3E0 -DFDC_IRQ=6 -DFDC_DMA=2

EXTRA_CFLAGS := $(FTAPE_OPT) $(FDC_OPT)

O_TARGET := ftape.o
O_OBJS   = kernel-interface.o tracing.o fdc-io.o fdc-isr.o \
	   ftape-bsm.o ftape-ctl.o ftape-eof.o ftape-read.o ftape-rw.o \
	   ftape-write.o ftape-io.o calibr.o ecc.o fc-10.o
M_OBJS   = $(O_TARGET)

include $(TOPDIR)/Rules.make
