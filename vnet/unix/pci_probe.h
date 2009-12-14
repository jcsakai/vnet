/* 
 *------------------------------------------------------------------
 * pci_probe.h - Linux-specific PCI-bus probe 
 *
 * Copyright (c) 2009 Cisco Systems, Inc. 
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *------------------------------------------------------------------
 */

#ifndef __included_pci_probe_h__
#define __included_pci_probe_h__

typedef struct {
    u16 vendor;                 
    u16 device;
    u32 reg_map_size;           /* mmap size */
    u8 *regbase;                /* mapped register VA */
    void *callback;            
} pci_probe_register_t;

typedef struct {
    pci_probe_register_t *pci_probe_registrations;    
    uword *pci_probe_hash;
    int initialized;
} pci_probe_main_t;

pci_probe_main_t pci_probe_main;

clib_error_t * pci_probe_init (vlib_main_t * vm);
void pci_probe_register (u16 vendor, u16 device, u32 reg_map_size, 
                         void *callback);

#endif /* __included_pci_probe_h__ */
