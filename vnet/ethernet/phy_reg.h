/*
 * phy_reg.h: ethernet phy registers
 *
 * Copyright (c) 2008 Eliot Dresselhaus
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
 */

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
 *
 * Modification to match BSD/OS 3.0 MII interface by Jason R. Thorpe,
 * Numerical Aerospace Simulation Facility, NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Manuel Bouyer.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef included_ethernet_phy_reg_h
#define included_ethernet_phy_reg_h

#include <clib/types.h>

/* Registers common to all Ethernet PHYs. */

#define	ETHERNET_PHY_MAX_PER_MII 32

#define	ETHERNET_PHY_BMCR	0x00 /* Basic mode control register (rw) */

#define	ETHERNET_PHY_BMCR_RESET		(1 << 15)
#define	ETHERNET_PHY_BMCR_LOOPBACK	(1 << 14)
#define	ETHERNET_PHY_BMCR_SPEED0	(1 << 13) /* speed selection (LSB) */
#define	ETHERNET_PHY_BMCR_AUTONEG_ENABLE (1 << 12)
#define	ETHERNET_PHY_BMCR_POWER_DOWN	(1 << 11)	/* power down */
#define	ETHERNET_PHY_BMCR_ISOLATE	(1 << 10)	/* isolate */
#define	ETHERNET_PHY_BMCR_AUTONEG_START (1 << 9) /* restart autonegotiation */
#define	ETHERNET_PHY_BMCR_FULL_DUPLEX	(1 << 8)
#define	ETHERNET_PHY_BMCR_HALF_DUPLEX	(0 << 8)
#define	ETHERNET_PHY_BMCR_COLLISION_TEST (1 << 7)
#define	ETHERNET_PHY_BMCR_SPEED1	(1 << 6) /* speed selection (MSB) */

#define	ETHERNET_PHY_BMCR_SPEED_10	0x0000
#define	ETHERNET_PHY_BMCR_SPEED_100	ETHERNET_PHY_BMCR_SPEED0
#define	ETHERNET_PHY_BMCR_SPEED_1000	ETHERNET_PHY_BMCR_SPEED1

#define	ETHERNET_PHY_BMCR_SPEED(x) \
  ((x) & (ETHERNET_PHY_BMCR_SPEED0|ETHERNET_PHY_BMCR_SPEED1))

/* Basic mode status register (ro) */
#define	ETHERNET_PHY_BMSR	0x01

#define	ETHERNET_PHY_BMSR_100T4			(1 << 15)
#define	ETHERNET_PHY_BMSR_100TX_FULL_DUPLEX	(1 << 14)
#define	ETHERNET_PHY_BMSR_100TX_HALF_DUPLEX	(1 << 13)
#define	ETHERNET_PHY_BMSR_10T_FULL_DUPLEX	(1 << 12)
#define	ETHERNET_PHY_BMSR_10T_HALF_DUPLEX	(1 << 11)
#define	ETHERNET_PHY_BMSR_100T2_FULL_DUPLEX	(1 << 10)
#define	ETHERNET_PHY_BMSR_100T2_HALF_DUPLEX	(1 << 9)	
#define	ETHERNET_PHY_BMSR_EXTENDED_STATUS	(1 << 8)
/* MII Frame Preamble Suppression */
#define	ETHERNET_PHY_BMSR_MFPS			(1 << 6)
#define	ETHERNET_PHY_BMSR_AUTONEG_DONE		(1 << 5)
#define	ETHERNET_PHY_BMSR_REMOTE_FAULT		(1 << 4)
#define	ETHERNET_PHY_BMSR_AUTONEG_CAPABLE	(1 << 3)
#define	ETHERNET_PHY_BMSR_LINK_UP		(1 << 2)
#define	ETHERNET_PHY_BMSR_JABBER_DETECTED	(1 << 1)
#define	ETHERNET_PHY_BMSR_EXTENDED_CAPABILITY	(1 << 0)

/*
 * Note that the EXTENDED_STATUS bit indicates that there is extended status
 * info available in register 15, but 802.3 section 22.2.4.3 also
 * states that that all 1000 Mb/s capable PHYs will set this bit to 1.
 */

#define	ETHERNET_PHY_BMSR_MEDIA_MASK		\
  (ETHERNET_PHY_BMSR_100T4			\
   | ETHERNET_PHY_BMSR_100TX_FULL_DUPLEX	\
   | ETHERNET_PHY_BMSR_100TX_HALF_DUPLEX	\
   | ETHERNET_PHY_BMSR_10T_FULL_DUPLEX		\
   | ETHERNET_PHY_BMSR_10T_HALF_DUPLEX		\
   | ETHERNET_PHY_BMSR_100T2_FULL_DUPLEX	\
   | ETHERNET_PHY_BMSR_100T2_HALF_DUPLEX)

/* Convert BMSR media capabilities to ANAR bits for autonegotiation.
 * Note the shift chopps off the ETHERNET_PHY_BMSR_ANEG bit. */
#define	ETHERNET_PHY_BMSR_MEDIA_TO_ANAR(x) \
 (((x) & ETHERNET_PHY_BMSR_MEDIA_MASK) >> 6)

/* ID registers 1 & 2 (ro) */
#define	ETHERNET_PHY_ID1 0x02
#define	ETHERNET_PHY_ID2 0x03

static inline u8 ethernet_phy_bit_reverse (u8 x)
{
  static unsigned char nibbletab[16] = {
    0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15
  };

  return ((nibbletab[x & 15] << 4) | nibbletab[x >> 4]);
}

/* Convert ID registers to OUI. */
static inline u32
ethernet_phy_id_oui (u32 id1, u32 id2)
{
  u32 h;

  h = (id1 << 6) | (id2 >> 10);

  return ((ethernet_phy_bit_reverse (h >> 16) << 16) |
	  (ethernet_phy_bit_reverse ((h >> 8) & 255) << 8) |
	  ethernet_phy_bit_reverse (h & 255));
}

static inline u32
ethernet_phy_id_model (u32 id1, u32 id2)
{ return (id2 >> 4) & 0x3f; }

static inline u32
ethernet_phy_id_revision (u32 id1, u32 id2)
{ return (id2 >> 0) & 0xf; }

/* Autonegotiation advertisement (rw) */
#define	ETHERNET_PHY_ANAR	0x04

/* Autonegotiation link partner abilities (rw).
   Same bits as ANAR. */
#define	ETHERNET_PHY_ANLPAR	0x05

/* section 28.2.4.1 and 37.2.6.1 */
#define ETHERNET_PHY_ANAR_NEXT_PAGE		(1 << 15)
#define	ETHERNET_PHY_ANAR_ACK			(1 << 14)
#define ETHERNET_PHY_ANAR_REMOTE_FAULT		(1 << 13)
#define	ETHERNET_PHY_ANAR_FLOW_CONTROL		(1 << 10)
#define	ETHERNET_PHY_ANAR_100T4			(1 << 9)
#define	ETHERNET_PHY_ANAR_100TX_FULL_DUPLEX	(1 << 8)
#define	ETHERNET_PHY_ANAR_100TX_HALF_DUPLEX	(1 << 7)
#define	ETHERNET_PHY_ANAR_10T_FULL_DUPLEX	(1 << 6)
#define	ETHERNET_PHY_ANAR_10T_HALF_DUPLEX	(1 << 5)

/* protocol selector CSMA/CD */
#define	ETHERNET_PHY_ANAR_CSMA			(1 << 0)

#define	ETHERNET_PHY_ANAR_1000X_FULL_DUPLEX	(1 << 5)
#define	ETHERNET_PHY_ANAR_1000X_HALF_DUPLEX	(1 << 6)

#define	ETHERNET_PHY_ANAR_1000X_PAUSE_MASK	(3 << 10)
#define	ETHERNET_PHY_ANAR_1000X_PAUSE_NONE	(0 << 10)
#define	ETHERNET_PHY_ANAR_1000X_PAUSE_SYM	(1 << 10)
#define	ETHERNET_PHY_ANAR_1000X_PAUSE_ASYM	(2 << 10)
#define	ETHERNET_PHY_ANAR_1000X_PAUSE_TOWARDS	(3 << 10)

/* section 28.2.4.1 and 37.2.6.1 */
#define	ETHERNET_PHY_ANER	0x06	/* Autonegotiation expansion (ro) */

/* section 28.2.4.1 and 37.2.6.1 */
#define ETHERNET_PHY_ANER_MULTIPLE_LINK_FAULT	(1 << 4)
#define ETHERNET_PHY_ANER_REMOTE_NEXT_PAGE	(1 << 3)
#define ETHERNET_PHY_ANER_NEXT_PAGE		(1 << 2)
#define ETHERNET_PHY_ANER_PAGE_RECEIVED		(1 << 1)
#define ETHERNET_PHY_ANER_REMOTE_AUTONEG	(1 << 0)

/* Autonegotiation next page */
#define	ETHERNET_PHY_ANNP	0x07
/* section 28.2.4.1 and 37.2.6.1 */

/* Autonegotiation link partner rx next page */
#define	ETHERNET_PHY_ANLPRNP	0x08
/* section 32.5.1 and 37.2.6.1 */

/* 100base-T2/1000baseT control register */
#define	ETHERNET_PHY_GTCR 0x09

/* see 802.3ab ss. 40.6.1.1.2 */
#define	ETHERNET_PHY_GTCR_TEST_MASK	0xe000

#define	ETHERNET_PHY_GTCR_MASTER_SLAVE_ENABLE	(1 << 12)
#define	ETHERNET_PHY_GTCR_ADV_MASTER		(1 << 11)
#define	ETHERNET_PHY_GTCR_ADV_SLAVE		(0 << 11)
#define	ETHERNET_PHY_GTCR_PORT_TYPE		(1 << 10) /* 1 DCE, 0 DTE */
#define	ETHERNET_PHY_GTCR_ADV_1000T_FULL_DUPLEX (1 << 9)
#define	ETHERNET_PHY_GTCR_ADV_1000T_HALF_DUPLEX (1 << 8)

/* 100base-T2/1000baseT status register */
#define	ETHERNET_PHY_GTSR	0x0a
#define	ETHERNET_PHY_GTSR_MASTER_SLAVE_FAULT	(1 << 15)
#define	ETHERNET_PHY_GTSR_MASTER_SLAVE		(1 << 14)
#define	ETHERNET_PHY_GTSR_IS_MASTER		(1 << 14)
#define	ETHERNET_PHY_GTSR_IS_SLAVE		(0 << 14)
#define	ETHERNET_PHY_GTSR_LOCAL_RX_STATUS_OK	(1 << 13)
#define	ETHERNET_PHY_GTSR_REMOTE_RX_STATUS_OK	(1 << 12)
#define	ETHERNET_PHY_GTSR_REMOTE_1000T_FULL_DUPLEX (1 << 11)
#define	ETHERNET_PHY_GTSR_REMOTE_1000T_HALF_DUPLEX (1 << 10)
#define	ETHERNET_PHY_GTSR_REMOTE_ASYM_PAUSE	(1 << 9)
#define	ETHERNET_PHY_GTSR_IDLE_ERROR		0x00ff	/* IDLE error count */

/* Extended status register */
#define	ETHERNET_PHY_EXTSR	0x0f
#define	ETHERNET_PHY_EXTSR_1000X_FULL_DUPLEX	(1 << 15)
#define	ETHERNET_PHY_EXTSR_1000X_HALF_DUPLEX	(1 << 14)
#define	ETHERNET_PHY_EXTSR_1000T_FULL_DUPLEX	(1 << 13)
#define	ETHERNET_PHY_EXTSR_1000T_HALF_DUPLEX	(1 << 12)

#define	ETHERNET_PHY_EXTSR_MEDIA_MASK		\
  (ETHERNET_PHY_EXTSR_1000X_FULL_DUPLEX		\
  | ETHERNET_PHY_EXTSR_1000X_HALF_DUPLEX	\
  | ETHERNET_PHY_EXTSR_1000T_FULL_DUPLEX	\
  | ETHERNET_PHY_EXTSR_1000T_HALF_DUPLEX)

#endif /* included_ethernet_phy_reg_h */
