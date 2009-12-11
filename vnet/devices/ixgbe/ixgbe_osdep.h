/******************************************************************************

  Copyright (c) 2001-2009, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD: $*/

#ifndef _IXGBE_OS_H_
#define _IXGBE_OS_H_

#include <vlib/vlib.h>
#include <clib/error.h>

#define usec_delay(x) \
    do { vlib_process_suspend (ixgbe_vlib_main, ((double)(x))*1e-6); } while (0)
#define msec_delay(x) \
    do { vlib_process_suspend (ixgbe_vlib_main, ((double)(x))*1e-3); } while (0)

#define DBG 0
#define MSGOUT(S, A, B)     clib_warning(S, A, B)
#define DEBUGFUNC(F)        DEBUGOUT(F);
#if DBG
	#define DEBUGOUT(S)         clib_warning(S)
	#define DEBUGOUT1(S,A)      clib_warning(S,A)
	#define DEBUGOUT2(S,A,B)    clib_warning(S,A,B)
	#define DEBUGOUT3(S,A,B,C)  clib_warning(S,A,B,C)
	#define DEBUGOUT6(S,A,B,C,D,E,F)  clib_warning(S,A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)  clib_warning(S,A,B,C,D,E,F,G)
#else
	#define DEBUGOUT(S)
	#define DEBUGOUT1(S,A)
	#define DEBUGOUT2(S,A,B)
	#define DEBUGOUT3(S,A,B,C)
	#define DEBUGOUT6(S,A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)
#endif

#define FALSE               0
#define false               0 /* shared code requires this */
#define TRUE                1
#define true                1
#define CMD_MEM_WRT_INVALIDATE          0x0010  /* BIT_4 */
#define PCI_COMMAND_REGISTER            PCIR_COMMAND
#define UNREFERENCED_PARAMETER(_p)


#define IXGBE_HTONL	htonl

typedef char s8;
typedef int bool;
typedef int s32;

#define le16_to_cpu 

#define mb()	__asm volatile("mfence" ::: "memory")
#define wmb()	__asm volatile("sfence" ::: "memory")
#define rmb()	__asm volatile("lfence" ::: "memory")

struct ixgbe_osdep
{
    char hunk_o_junk;
};

/* These routines are needed by the shared code */
struct ixgbe_hw; 
// extern u16 ixgbe_read_pci_cfg(struct ixgbe_hw *, u32);
#define IXGBE_READ_PCIE_WORD ixgbe_read_pci_cfg

// extern void ixgbe_write_pci_cfg(struct ixgbe_hw *, u32, u16);
#define IXGBE_WRITE_PCIE_WORD ixgbe_write_pci_cfg

#define IXGBE_WRITE_FLUSH(a) IXGBE_READ_REG(a, IXGBE_STATUS)

#define IXGBE_READ_REG(a, reg) \
    (*((volatile u32 *)((u8 *)((a)->hw_addr)+(reg))))

#define IXGBE_WRITE_REG(a, reg, value) \
    do { (*((volatile u32 *)((u8 *)((a)->hw_addr)+(reg))))=(value);} while (0)

#endif /* _IXGBE_OS_H_ */
