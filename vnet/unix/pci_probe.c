/* 
 *------------------------------------------------------------------
 * pci_probe.c - Linux-specific PCI-bus probe 
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/pci.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <clib/types.h>
#include <clib/hash.h>
#include <clib/format.h>
#include <clib/error.h>
#include <clib/unix.h>
#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

#include <vnet/unix/pci_probe.h>

pci_probe_main_t pci_probe_main;


static void pci_probe_initialize(pci_probe_main_t *pm)
{
    pm->pci_probe_hash = hash_create (0, sizeof(uword));
    pm->initialized = 1;
}

void pci_probe_register (u16 vendor, u16 device, u32 reg_map_size, 
                         void *callback)
{
    uword *p;
    pci_probe_main_t *pm = &pci_probe_main;
    pci_probe_register_t *regp;
    u32 vdev;

    if (!pm->initialized)
        pci_probe_initialize(pm);
    
    vdev = (vendor<<16) | device;
    p = hash_get (pm->pci_probe_hash, vdev);
    if (p) {
        clib_warning("duplicate pci probe reg for vendor 0x%04x, dev 0x%04x",
                     vendor, device);
        return;
    }
    vec_add2 (pm->pci_probe_registrations, regp, 1);
    regp->vendor = vendor;
    regp->device = device;
    regp->reg_map_size = reg_map_size;
    regp->callback = callback;

    hash_set (pm->pci_probe_hash, vdev, regp - pm->pci_probe_registrations);
}

static clib_error_t *read_file_contents (char *name, u8 **contentsp)
{
    int fd;
    int nread;
    u8 *buf = 0;
    u32 offset;

    fd = open (name, O_RDONLY);

    if (fd < 0) {
        return clib_error_return_unix (0, "open '%s'", name);
    }

    vec_validate (buf, 4096);
    offset = 0;

    while (1) {
        nread = read (fd, buf + offset, 4096);
        if (nread < 0) {
            *contentsp = 0;
            vec_free (buf);
            return clib_error_return_unix (0, "read error '%s'", name);
        }
        offset += nread;
        if (nread == 0) {
            _vec_len(buf) = offset;
            *contentsp = buf;
            return 0;
        }
        vec_validate (buf, offset + 4095);
    }
}

static int parsex (u8 **datap, u32 *rvp)
{
    u8 *cp = *datap;
    u32 rv = 0;

    while (*cp && (*cp == ' ' || *cp == '\t'))
        cp++;

    while (*cp && 
           (((*cp >= '0') && (*cp <= '9'))
            || ((*cp >= 'a') && (*cp <= 'f'))
            || ((*cp >= 'A') && (*cp <= 'F')))) {
        rv = (rv << 4);
        if ((*cp >= '0') && (*cp <= '9'))
            rv += *cp - '0';
        else if ((*cp >= 'a') && (*cp <= 'f'))
            rv += (*cp - 'a') + 10;
        else
            rv += (*cp - 'A') + 10;
        cp++;
    }
    *datap = cp;
    *rvp = rv;
    return 0;
}

static int parseX (u8 **datap, u64 *rvp)
{
    u8 *cp = *datap;
    u64 rv = 0;;

    while (*cp && (*cp == ' ' || *cp == '\t'))
        cp++;

    while (*cp && 
           (((*cp >= '0') && (*cp <= '9'))
            || ((*cp >= 'a') && (*cp <= 'f'))
            || ((*cp >= 'A') && (*cp <= 'F')))) {
        rv = (rv << 4);
        if ((*cp >= '0') && (*cp <= '9'))
            rv += *cp - '0';
        else if ((*cp >= 'a') && (*cp <= 'f'))
            rv += (*cp - 'a') + 10;
        else
            rv += (*cp - 'A') + 10;
        cp++;
    }
    *datap = cp;
    *rvp = rv;
    return 0;
}

static int parsestr (u8 **datap, u8 **rvp)
{
    u8 *cp = *datap;
    u8 *driver_name = 0;

    while (*cp && (*cp == ' ' || *cp == '\t'))
        cp++;

    if (*cp == '\n') {
        *rvp = 0;
        *datap = cp + 1;
        return 0;
    }

    while (*cp && *cp != '\n') {
        vec_add1(driver_name, *cp);
        cp++;
    }
    vec_add1(driver_name, 0);
    if (*cp)
        cp++;

    *datap = cp;
    *rvp = driver_name;
    return 0;
}

static void pci_bus_probe (pci_probe_main_t *pm)
{
    clib_error_t * error;
    u8 *contents;
    u8 *cp;
    int i;

    error = read_file_contents ("/proc/bus/pci/devices", &contents);

    if (error) {
        clib_error_report(error);
        return;
    }

    vec_add1(contents, 0);

    /* 
     * For whatever reason, google (/proc/bus/pci/devices) produces
     * Jack Shit. So, "Use the Force and Read the Source!"
     *
     * /proc/bus/pci/devices is created by the kernel in
     * .../linux/drivers/pci/proc.c:show_device()
     *
     * Multiple lines, one per device, of the form:
     *
     * %2x - bus number
     * %2x - devfn
     * <tab>
     * %4x - vendor
     * %4x - device id
     * <tab>
     * %x  - irq
     * 14 x {
     *    <tab>
     *    %16llx "resources". The first resource is the device's
     *                        primary (memory) BAR.
     * }
     * %s - driver name (only if driver has grabbed the device, presumably)
     */

    cp = contents;

    while (*cp) {
        u16 bus, devfn, vendor, device;
        u32 vdev;
        u32 irq;
        u64 bar = 0;
        u64 resources [14];
        u8 *driver;
        uword *p;
        u8 *pci_device_name;
        int fd;
        u32 bus_devfn, vend_dev;

        clib_error_t *(*fp)(int fd, u8 *regbase, u64 *resources,
                            u16 bus, u16 devfn, u32 irq);
        pci_probe_register_t *regp;

        if (parsex (&cp, &bus_devfn))
            clib_warning ("bus_devfn");
        if (parsex (&cp, &vend_dev))
            clib_warning ("vend_dev");
        if (parsex (&cp, &irq))
            clib_warning ("irq");

        for (i = 0; i < 14; i++) {
            if (parseX (&cp, &resources[i]))
                clib_warning ("resources[%d]", i);
            if (i == 0)
                bar = resources[i];
        }
        parsestr (&cp, &driver);

        /* Refer to the secret decoder ring above */
        bus = (bus_devfn>>8) & 0xFF;
        devfn = bus_devfn & 0xFF;

        vendor = vend_dev>>16;
        device = vend_dev & 0xFFFF;

        vdev = (vendor<<16) | device;
        p = hash_get (pm->pci_probe_hash, vdev);
        if (p == 0) {
#if DEBUG > 1
            fformat(stderr, 
                    "%2x:%2x.%d: no reg for vendor 0x%x, device 0x%x",
                    bus, PCI_SLOT(devfn), PCI_FUNC(devfn),
                    vendor, device);
            if (driver) {
                fformat(stderr, " driver %s\n", driver);
                vec_free(driver);
            } else {
                fformat(stderr, "\n");
            }
                            

#endif
            continue;
        }
    
        regp = pm->pci_probe_registrations + p[0];
        
#if DEBUG
        fformat(stderr, 
                "%2x:%2x.%d: found reg for vendor 0x%x, device 0x%x",
                bus, PCI_SLOT(devfn), PCI_FUNC(devfn),
                vendor, device);
        if (driver) {
            fformat(stderr, " driver %s [BAD IDEA!]\n", driver);
            vec_free(driver);
        } else {
            fformat(stderr, "\n");
        }
#endif

        pci_device_name = format (0, "/proc/bus/pci/%02x/%02d.%d%c",
                                  bus, PCI_SLOT(devfn), PCI_FUNC(devfn), 0);
        
        fd = open ((char *)pci_device_name, O_RDWR);
        if (fd < 0) {
            clib_unix_warning ("Couldn't open %s", pci_device_name);
            vec_free(pci_device_name);
            continue;
        }
        
        if (ioctl(fd, PCIIOC_MMAP_IS_MEM) < 0) {
            clib_unix_warning ("PCIIOC_MMAP_IS_MEM");
            close(fd);
            vec_free(pci_device_name);
            continue;
        }

        vec_free (pci_device_name);
        
        if (bar == 0) {
            clib_warning ("BAR is zero?");
        }

        regp->regbase = 0;

        /* Map the primary BAR, probably sufficient for most things */
        if (regp->reg_map_size) {
            regp->regbase = mmap (0, regp->reg_map_size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, (off_t) bar);
            
            if (regp->regbase == MAP_FAILED) {
                clib_unix_warning ("mmap");
                continue;
            }
        }
                              
        /* 
         * Call the registered callback. Pass along the mapping we
         * created (if any), and the resource array 
         */
        fp = regp->callback;
        error = (*fp)(fd, regp->regbase, resources, bus, devfn, irq);

        close(fd);

        if (error) {
            clib_error_report(error);
            if (regp->regbase)
                munmap (regp->regbase, regp->reg_map_size);
        }
    }
    vec_free(contents);
}

static clib_error_t *
pci_probe_config (vlib_main_t *vm, unformat_input_t * input)
{
    pci_bus_probe (&pci_probe_main);

    return 0;
}

VLIB_CONFIG_FUNCTION (pci_probe_config, "pci_probe");
/* call in main() to force the linker to load this module... */
clib_error_t *
pci_probe_init (vlib_main_t * vm)
{
    return 0;
}
VLIB_INIT_FUNCTION (pci_probe_init);
