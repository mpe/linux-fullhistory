/* wim.h: Defines the layout of the "Window Invalid Register" on
          Version 8 of the Sparc Architecture.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

#ifndef __LINUX_SPARC_WIM_H
#define __LINUX_SPARC_WIM_H

#ifdef __LINUX_SPARC_V8     /* register doesn't exist on the V9 */

/* The Window Invalid Register %wim, holds a set of which register
   windows are 'valid' at this point in time.

   ------------------------------------------------------------
   |W31|W30|W29|W28|W27|W26|W25|W24|W23|....|W5|W4|W3|W2|W1|W0|
   ------------------------------------------------------------

   Each register window on the chip gets one bit. If the bit is
   set then the window is currently 'invalid' and hardware will
   trap if that window is entered via a 'save', 'restore', or
   'rett' instruction. Privileged software is responsible for
   updating this on trap fills/spills etc. Therefore if a 'save'
   instruction is executed and it causes the Current Window
   Pointer to equal a register window which has its bit set in
   %wim we get a 'overflow' trap, a restore into such a register
   invokes a window 'spill' trap.
*/

#define __LINUX_SPARC_HAS_WIM

/* Macro to fine the %wim bit mask for the current window pointer */
#define CWP_TO_WIM_MASK(cwp)  (1<<(cwp))

/* Assembly version of above macro, 'cwp' and 'wimask' must be registers */
#define ASM_CWP_TO_WIM_MASK(cwp,wimask) \
          or  %g0, 0x1, wimask \
          sll wimask, cwp, wimask

/* Assembly macro to find if the given window is set to invalid in the %wim.
   Again 'window', 'result', and 'scratch' must be in registers. This leaves 
   a non-zero value in result if the window is indeed invalid. This routine
   works because we keep exactly one window invalid at all times to maximize
   register utilization, which means both kernel and user windows can be in
   the register file at the same time in certain trap situations.
*/
#define ASM_REG_WIN_INVAL(window,result,scratch) \
         rd  %wim, result \
         or  %g0, 0x1, scratch \
         sll scratch, window, scratch \
         and scratch, result, result

#endif /* !(__LINUX_SPARC_V8) */

#endif  /* !(__LINUX_SPARC_WIM_H) */

