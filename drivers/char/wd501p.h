/*
 *	Industrial Computer Source WDT500/501 driver for Linux 1.3.x
 *
 *	(c) Copyright 1995	CymruNET Ltd
 *				Innovation Centre
 *				Singleton Park
 *				Swansea
 *				Wales
 *				UK
 *				SA2 8PP
 *
 *	http://www.cymru.net
 *
 *	This driver is provided under the GNU public license, incorporated
 *	herein by reference. The driver is provided without warranty or 
 *	support.
 *
 *	Release 0.04.
 *
 */

#include <linux/config.h>

#define WDT_COUNT0		(io+0)
#define WDT_COUNT1		(io+1)
#define WDT_COUNT2		(io+2)
#define WDT_CR			(io+3)
#define WDT_SR			(io+4)
#define WDT_RT			(io+5)
#define WDT_UNUSED		(io+6)
#define WDT_DC			(io+7)

#define WDC_SR_WCCR		1	/* Active low */
#define WDC_SR_TGOOD		2
#define WDC_SR_ISOI0		4
#define WDC_SR_ISII1		8
#define WDC_SR_FANGOOD		16
#define WDC_SR_PSUOVER		32	/* Active low */
#define WDC_SR_PSUUNDR		64	/* Active low */
#define WDC_SR_IRQ		128	/* Active low */

/*
 *	Feature Map 1 is the active high inputs not supported on your card.
 *	Feature Map 2 is the active low inputs not supported on your card.
 */
 
#ifdef CONFIG_WDT_501		/* Full board */

#ifdef CONFIG_WDT501_FAN	/* Full board, Fan has no tachometer */
#define FEATUREMAP1		0
#define WDT_OPTION_MASK		(WDIOF_OVERHEAT|WDIOF_POWERUNDER|WDIOF_POWEROVER|WDIOF_EXTERN1|WDIOF_EXTERN2|WDIOF_FANFAULT)
#else
#define FEATUREMAP1		WDC_SR_FANGOOD
#define WDT_OPTION_MASK		(WDIOF_OVERHEAT|WDIOF_POWERUNDER|WDIOF_POWEROVER|WDIOF_EXTERN1|WDIOF_EXTERN2)
#endif

#define FEATUREMAP2		0
#endif

#ifndef CONFIG_WDT_501
#define CONFIG_WDT_500
#endif

#ifdef CONFIG_WDT_500		/* Minimal board */
#define FEATUREMAP1		(WDC_SR_TGOOD|WDC_SR_FANGOOD)
#define FEATUREMAP2		(WDC_SR_PSUOVER|WDC_SR_PSUUNDR)
#define WDT_OPTION_MASK		(WDIOF_OVERHEAT)
#endif

#ifndef FEATUREMAP1
#error "Config option not set"
#endif
