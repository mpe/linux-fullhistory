/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: board.h
 *
 *    Description:
 *	A generic include file which pulls in appropriate include files
 *      for specific board types based on configuration settings.
 *
 */

#ifndef __BOARD_H__
#define	__BOARD_H__

#include <linux/config.h>

#if defined(CONFIG_OAK)
#include <asm/oak.h>
#endif

#if defined(CONFIG_WALNUT)
#include <asm/walnut.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif



#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H__ */
