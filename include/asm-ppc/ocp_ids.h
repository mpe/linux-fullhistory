/*
 * FILE NAME: ocp_ids.h
 *
 * BRIEF MODULE DESCRIPTION:
 * OCP device ids based on the ideas from PCI
 *
 * Maintained by: Armin <akuster@mvista.com>
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Version 1.0 08/22/02 -Armin
 *  	initial release
 */

/*
 * Vender  device
 * [xxxx]  [xxxx]
 *
 *  Keep in order, please
 */

/* Vendor IDs 0x0001 - 0xFFFF copied from pci_ids.h */

#define	OCP_VENDOR_INVALID	0x0000
#define	OCP_VENDOR_ARM		0x0004
#define OCP_VENDOR_IBM		0x1014
#define OCP_VENDOR_MOTOROLA	0x1057
#define	OCP_VENDOR_XILINX	0x10ee
#define	OCP_VENDOR_UNKNOWN	0xFFFF

/* device identification */

/* define type */
#define OCP_FUNC_INVALID	0x0000

/* system 0x0001 - 0x001F */
#define	OCP_FUNC_UIC		0x0001

/* Timers 0x0020 - 0x002F */
#define OCP_FUNC_GPT		0x0020 	/* General purpose timers */
#define OCP_FUNC_RTC		0x0021

/* Serial 0x0030 - 0x006F*/
#define OCP_FUNC_16550		0x0031
#define OCP_FUNC_SSP		0x0032 /* sync serial port */
#define OCP_FUNC_SCP		0x0033 	/* serial controller port */
#define OCP_FUNC_SCC		0x0034 	/* serial contoller */
#define OCP_FUNC_SCI		0x0035 	/* Smart card */
#define OCP_FUNC_IIC		0x0040
#define OCP_FUNC_USB		0x0050
#define OCP_FUNC_IR		0x0060	

/* Memory devices 0x0090 - 0x009F */
#define	OCP_FUNC_SDRAM		0x0091
#define OCP_FUNC_DMA		0x0092

/* Display 0x00A0 - 0x00AF */
#define OCP_FUNC_VIDEO		0x00A0
#define OCP_FUNC_LED		0x00A1
#define	OCP_FUNC_LCD		0x00A2

/* Sound 0x00B0 - 0x00BF */
#define OCP_FUNC_AUDIO		0x00B0

/* Mass Storage 0x00C0 - 0xxCF */
#define OCP_FUNC_IDE		0x00C0

/* Misc 0x00D0 - 0x00DF*/
#define OCP_FUNC_GPIO		0x00D0
#define OCP_FUNC_ZMII		0x00D1

/* Network 0x0200 - 0x02FF */
#define OCP_FUNC_EMAC		0x0200

/* Bridge devices 0xE00 - 0xEFF */
#define OCP_FUNC_HOST		0x0E00
#define OCP_FUNC_DCR		0x0E01
#define OCP_FUNC_OPB		0x0E02
#define OCP_FUNC_PHY		0x0E03
#define OCP_FUNC_EXT		0x0E04
#define	OCP_FUNC_PCI		0x0E05
#define	OCP_FUNC_PLB		0x0E06

#define OCP_FUNC_UNKNOWN	0xFFFF
