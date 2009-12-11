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


#ifndef _IXGBE_H_
#define _IXGBE_H_

#include "ixgbe_api.h"

/* Tunables */

/*
 * TxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of transmit descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more transmits. Each descriptor is 16
 * bytes. Performance tests have show the 2K value to be optimal for top
 * performance.
 */
#define DEFAULT_TXD	1024
#define PERFORM_TXD	2048
#define MAX_TXD		4096
#define MIN_TXD		64

/*
 * RxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of receive descriptors allocated for each RX queue. Increasing this
 * value allows the driver to buffer more incoming packets. Each descriptor
 * is 16 bytes.  A receive buffer is also allocated for each descriptor. 
 * 
 * Note: with 8 rings and a dual port card, it is possible to bump up 
 *	against the system mbuf pool limit, you can tune nmbclusters
 *	to adjust for this.
 */
#define DEFAULT_RXD	1024
#define PERFORM_RXD	2048
#define MAX_RXD		4096
#define MIN_RXD		64

/* Alignment for rings */
#define DBA_ALIGN	128

/*
 * This parameter controls the maximum no of times the driver will loop in
 * the isr. Minimum Value = 1
 */
#define MAX_LOOP	10

/*
 * This parameter controls the duration of transmit watchdog timer.
 */
#define IXGBE_TX_TIMEOUT                   5	/* set to 5 seconds */

/*
 * This parameters control when the driver calls the routine to reclaim
 * transmit descriptors.
 */
#define IXGBE_TX_CLEANUP_THRESHOLD	(adapter->num_tx_desc / 8)
#define IXGBE_TX_OP_THRESHOLD		(adapter->num_tx_desc / 32)

#define IXGBE_MAX_FRAME_SIZE	0x3F00

/* Flow control constants */
#define IXGBE_FC_PAUSE		0x680
#define IXGBE_FC_HI		0x20000
#define IXGBE_FC_LO		0x10000

/* Defines for printing debug information */
#define DEBUG_INIT  0
#define DEBUG_IOCTL 0
#define DEBUG_HW    0

#define INIT_DEBUGOUT(S)            if (DEBUG_INIT)  clib_warning(S)
#define INIT_DEBUGOUT1(S, A)        if (DEBUG_INIT)  clib_warning(S, A)
#define INIT_DEBUGOUT2(S, A, B)     if (DEBUG_INIT)  clib_warning(S, A, B)
#define IOCTL_DEBUGOUT(S)           if (DEBUG_IOCTL) clib_warning(S)
#define IOCTL_DEBUGOUT1(S, A)       if (DEBUG_IOCTL) clib_warning(S, A)
#define IOCTL_DEBUGOUT2(S, A, B)    if (DEBUG_IOCTL) clib_warning(S, A, B)
#define HW_DEBUGOUT(S)              if (DEBUG_HW) clib_warning(S )
#define HW_DEBUGOUT1(S, A)          if (DEBUG_HW) clib_warning(S, A)
#define HW_DEBUGOUT2(S, A, B)       if (DEBUG_HW) clib_warning(S, A, B)

#define MAX_NUM_MULTICAST_ADDRESSES     128
#define IXGBE_82598_SCATTER		100
#define IXGBE_82599_SCATTER		32
#define MSIX_82598_BAR			3
#define MSIX_82599_BAR			4
#define IXGBE_TSO_SIZE			65535
#define IXGBE_TX_BUFFER_SIZE		((u32) 1514)
#define IXGBE_RX_HDR			256
#define IXGBE_VFTA_SIZE			128
#define IXGBE_BR_SIZE			4096
#define CSUM_OFFLOAD			7	/* Bits in csum flags */

/*
 * Interrupt Moderation parameters 
 */
#define IXGBE_LOW_LATENCY	128
#define IXGBE_AVE_LATENCY	400
#define IXGBE_BULK_LATENCY	1200
#define IXGBE_LINK_ITR		2000

/* Header split args for get_bug */
#define IXGBE_CLEAN_HDR		1
#define IXGBE_CLEAN_PKT		2
#define IXGBE_CLEAN_ALL		3

/*
 *****************************************************************************
 * vendor_info_array
 * 
 * This array contains the list of Subvendor/Subdevice IDs on which the driver
 * should load.
 * 
 *****************************************************************************
 */
typedef struct _ixgbe_vendor_info_t {
	unsigned int    vendor_id;
	unsigned int    device_id;
	unsigned int    subvendor_id;
	unsigned int    subdevice_id;
	unsigned int    index;
} ixgbe_vendor_info_t;


#if 0
struct ixgbe_tx_buf {
	u32		eop_index;
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};

struct ixgbe_rx_buf {
	struct mbuf	*m_head;
	struct mbuf	*m_pack;
	bus_dmamap_t	map;
};

/*
 * Bus dma allocation structure used by ixgbe_dma_malloc and ixgbe_dma_free.
 */
struct ixgbe_dma_alloc {
	bus_addr_t		dma_paddr;
	caddr_t			dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

/*
 * The transmit ring, one per tx queue
 */
struct tx_ring {
        struct adapter		*adapter;
	struct mtx		tx_mtx;
	u32			me;
	u32			msix;
	u32			watchdog_timer;
	union ixgbe_adv_tx_desc	*tx_base;
	volatile u32		tx_hwb;
	struct ixgbe_dma_alloc	txdma;
	struct task     	tx_task;
	struct taskqueue	*tq;
	u32			next_avail_tx_desc;
	u32			next_tx_to_clean;
	struct ixgbe_tx_buf	*tx_buffers;
	volatile u16		tx_avail;
	u32			txd_cmd;
	bus_dma_tag_t		txtag;
	char			mtx_name[16];
#if __FreeBSD_version >= 800000
	struct buf_ring		*br;
#endif
	/* Interrupt resources */
	void			*tag;
	struct resource		*res;
#ifdef IXGBE_FDIR
	u16			atr_sample;
	u16			atr_count;
#endif
	/* Soft Stats */
	u32			no_tx_desc_avail;
	u32			no_tx_desc_late;
	u64			tx_irq;
	u64			total_packets;
};


/*
 * The Receive ring, one per rx queue
 */
struct rx_ring {
        struct adapter		*adapter;
	struct mtx		rx_mtx;
	u32			me;
	u32			msix;
	u32			payload;
	struct task     	rx_task;
	struct taskqueue	*tq;
	union ixgbe_adv_rx_desc	*rx_base;
	struct ixgbe_dma_alloc	rxdma;
	struct lro_ctrl		lro;
	bool			lro_enabled;
	bool			hdr_split;
	bool			hw_rsc;
        unsigned int		last_cleaned;
        unsigned int		next_to_check;
	struct ixgbe_rx_buf	*rx_buffers;
	bus_dma_tag_t		rxtag;
	bus_dmamap_t		spare_map;
	char			mtx_name[16];

	u32			bytes; /* Used for AIM calc */
	u32			eitr_setting;

	/* Interrupt resources */
	void			*tag;
	struct resource		*res;

	/* Soft stats */
	u64			rx_irq;
	u64			rx_split_packets;
	u64			rx_packets;
	u64 			rx_bytes;
	u64 			rsc_num;
#ifdef IXGBE_FDIR
	u64			flm;
#endif
};
#endif

/* Our adapter structure */
struct adapter {
	struct ixgbe_hw	hw;

        struct ixgbe_port *port; 
        u32 if_flags;          /* ported BSD if_flags */
        u32 if_mtu;            /* ported BSD mtu */
        u32 if_capenable;      /* ported BSD capabilities */
        u32 if_hwassist;       /* ported BSD */
        u32 num_vlans;         /* ported BSD */

	struct ixgbe_osdep	osdep;
	struct device	*dev;

	struct resource	*pci_mem;
	struct resource	*msix_mem;

	/*
	 * Interrupt resources: this set is
	 * either used for legacy, or for Link
	 * when doing MSIX
	 */
	void		*tag;
	struct resource *res;

#if 0
	struct ifmedia	media;
	struct callout	timer;
	int		msix;
	int		if_flags;

	struct mtx	core_mtx;

	eventhandler_tag vlan_attach;
	eventhandler_tag vlan_detach;

	u16		num_vlans;
#endif
	u16		num_queues;

	/* Info about the board itself */
	u32		optics;
	bool		link_active;
	u16		max_frame_size;
	u32		link_speed;
	bool		link_up;
	u32 		linkvec;

	/* Mbuf cluster size */
	u32		rx_mbuf_sz;

	/* Support for pluggable optics */
	bool		sfp_probe;
#if 0
	struct task     link_task; 	/* Link tasklet */
	struct task     mod_task; 	/* SFP tasklet */
	struct task     msf_task; 	/* Multispeed Fiber tasklet */
#endif
#ifdef IXGBE_FDIR
	int			fdir_reinit;
	struct task     	fdir_task;
#endif
	struct taskqueue	*tq;

#if 0
	/*
	 * Transmit rings:
	 *	Allocated at run time, an array of rings.
	 */
	struct tx_ring	*tx_rings;
	int		num_tx_desc;

	/*
	 * Receive rings:
	 *	Allocated at run time, an array of rings.
	 */
	struct rx_ring	*rx_rings;
	int		num_rx_desc;
	u64		rx_mask;
	u32		rx_process_limit;
#endif

#ifdef IXGBE_IEEE1588
	/* IEEE 1588 precision time support */
	struct cyclecounter     cycles;
	struct nettimer         clock;
	struct nettime_compare  compare;
	struct hwtstamp_ctrl    hwtstamp;
#endif

	/* Misc stats maintained by the driver */
	unsigned long   dropped_pkts;
	unsigned long   mbuf_defrag_failed;
	unsigned long   mbuf_header_failed;
	unsigned long   mbuf_packet_failed;
	unsigned long   no_tx_map_avail;
	unsigned long   no_tx_dma_setup;
	unsigned long   watchdog_events;
	unsigned long   tso_tx;
	unsigned long	link_irq;

	struct ixgbe_hw_stats stats;
};

/* Precision Time Sync (IEEE 1588) defines */
#define ETHERTYPE_IEEE1588      0x88F7
#define PICOSECS_PER_TICK       20833
#define TSYNC_UDP_PORT          319 /* UDP port for the protocol */
#define IXGBE_ADVTXD_TSTAMP	0x00080000


#define IXGBE_CORE_LOCK_INIT(_sc, _name)
#define IXGBE_CORE_LOCK_DESTROY(_sc)
#define IXGBE_TX_LOCK_DESTROY(_sc)  
#define IXGBE_RX_LOCK_DESTROY(_sc)  
#define IXGBE_CORE_LOCK(_sc)        
#define IXGBE_TX_LOCK(_sc)          
#define IXGBE_TX_TRYLOCK(_sc)       
#define IXGBE_RX_LOCK(_sc)          
#define IXGBE_CORE_UNLOCK(_sc)      
#define IXGBE_TX_UNLOCK(_sc)        
#define IXGBE_RX_UNLOCK(_sc)        
#define IXGBE_CORE_LOCK_ASSERT(_sc) 
#define IXGBE_TX_LOCK_ASSERT(_sc)   

static inline bool
ixgbe_is_sfp(struct ixgbe_hw *hw)
{
	switch (hw->phy.type) {
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
	case ixgbe_phy_tw_tyco:
	case ixgbe_phy_tw_unknown:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Definitions to make BSD driver code compile */

#define ETHERMTU	1500
#define ETHER_MIN_LEN   64
#define MCLBYTES 	2048
#define MJUMPAGESIZE	(4<<10)

#define IFM_10G_CX4     0x00000001
#define IFM_10G_LR      0x00000002
#define IFM_10G_SR      0x00000004
#define IFM_1000_T      0x00000008
#define IFM_ACTIVE      0x00000010
#define IFM_AUTO        0x00000020
#define IFM_AVALID      0x00000040
#define IFM_ETHER       0x00000080
#define IFM_FDX	        0x00000100
#define IFM_IMASK      	0x00000200

#define IFF_PROMISC 	0x00000001
#define IFF_ALLMULTI	0x00000002

#define IFCAP_HWCSUM		0x00000001
#define IFCAP_JUMBO_MTU		0x00000002
#define IFCAP_LRO		0x00000004
#define IFCAP_RXCSUM		0x00000008
#define IFCAP_TSO4		0x00000010
#define IFCAP_TXCSUM		0x00000020
#define IFCAP_VLAN_HWCSUM	0x00000040
#define IFCAP_VLAN_HWTAGGING	0x00000080
#define IFCAP_VLAN_MTU		0x00000100

#define CSUM_DATA_VALID		0x00000001
#define CSUM_IP_CHECKED		0x00000002
#define CSUM_IP_VALID		0x00000004
#define CSUM_PSEUDO_HDR		0x00000010
#define CSUM_TCP 		0x00000020
#define CSUM_TSO		0x00000040
#define CSUM_UDP		0x00000080


#endif /* _IXGBE_H_ */
