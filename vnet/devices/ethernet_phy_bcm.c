/*
 * phy_bcm.c: Broadcom ethernet PHY driver
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
 * Copyright (c) 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * FreeBSD: src/sys/dev/mii/brgphyreg.h,v 1.1 2000/04/22 01:58:17 wpaul Exp
 */

#include <vnet/ethernet/phy.h>

/* Broadcom BCM5400 registers. */

#define BRGPHY_EXTCTL	0x10	/* PHY extended control */
#define BRGPHY_EXTCTL_10_BIT_INTERFACE		(1 << 15)
#define BRGPHY_EXTCTL_MII_INTERFACE		(0 << 15)
#define BRGPHY_EXTCTL_DISABLE_MDI_CROSSOVER	(1 << 14)
#define BRGPHY_EXTCTL_DISABLE_TX		(1 << 13)
#define BRGPHY_EXTCTL_DISABLE_INTERRUPT		(1 << 12)
#define BRGPHY_EXTCTL_FORCE_INTERRUPT		(1 << 11)
#define BRGPHY_EXTCTL_BYPASS_4B5B		(1 << 10)
#define BRGPHY_EXTCTL_BYPASS_SCRAMBLER		(1 << 9)
#define BRGPHY_EXTCTL_BYPASS_MLT3		(1 << 8)
#define BRGPHY_EXTCTL_BYPASS_RX_ALIGNMENT	(1 << 7)
#define BRGPHY_EXTCTL_RESET_SCRAMBLER		(1 << 6)
#define BRGPHY_EXTCTL_ENABLE_TRAFFIC_LED_MODE	(1 << 5)
#define BRGPHY_EXTCTL_FORCE_LED_ON		(1 << 4)
#define BRGPHY_EXTCTL_FORCE_LED_OFF		(1 << 3)
#define BRGPHY_EXTCTL_EXTENDED_IPG		(1 << 2)
#define BRGPHY_EXTCTL_3LINK_LED			(1 << 1)
#define BRGPHY_EXTCTL_FIFO_HIGH_LATENCY		(1 << 0)

#define BRGPHY_EXTSTS	0x11	/* PHY extended status */
#define BRGPHY_EXTSTS_AUTONEG_MISMATCH		(1 << 15)
#define BRGPHY_EXTSTS_WIRESPEED_DOWNGRADE	(1 << 14)
#define BRGPHY_EXTSTS_MDI_CROSSOVER		(1 << 13)
#define BRGPHY_EXTSTS_INTERRUPT_ACTIVE		(1 << 12)
#define BRGPHY_EXTSTS_REMOTE_RX_UP		(1 << 11)
#define BRGPHY_EXTSTS_LOCAL_RX_UP		(1 << 10)
#define BRGPHY_EXTSTS_DESCRAMBLER_LOCKED	(1 << 9)
#define BRGPHY_EXTSTS_LINK_UP			(1 << 8)
#define BRGPHY_EXTSTS_CRC_ERROR			(1 << 7)
#define BRGPHY_EXTSTS_CARRIER_EXTENSION_ERROR	(1 << 6)
#define BRGPHY_EXTSTS_BAD_SSD			(1 << 5)
#define BRGPHY_EXTSTS_BAD_ESD			(1 << 4)
#define BRGPHY_EXTSTS_RX_ERROR			(1 << 3)
#define BRGPHY_EXTSTS_TX_ERROR			(1 << 2)
#define BRGPHY_EXTSTS_LOCK_ERROR		(1 << 1)
#define BRGPHY_EXTSTS_MLT3_ERROR		(1 << 0)

#define BRGPHY_RX_ERROR_COUNT 0x12

#define BRGPHY_FALSE_CARRIER_SENSE_COUNT 0x13

#define BRGPHY_RX_NOT_OK_COUNT 0x14
#define BRGPHY_RX_NOT_OK_LOCAL(x) (x >> 8)
#define BRGPHY_RX_NOT_OK_REMOTE(x) (x & 0xff)

#define BRGPHY_MII_DSP_RW_PORT	0x15	/* DSP coefficient r/w port */

#define BRGPHY_MII_DSP_ADDR_REG	0x17	/* DSP coefficient addr register */

#define BRGPHY_DSP_TAP_NUMBER_MASK		0x00
#define BRGPHY_DSP_AGC_A			0x00
#define BRGPHY_DSP_AGC_B			0x01
#define BRGPHY_DSP_MSE_PAIR_STATUS		0x02
#define BRGPHY_DSP_SOFT_DECISION		0x03
#define BRGPHY_DSP_PHASE_REG			0x04
#define BRGPHY_DSP_SKEW				0x05
#define BRGPHY_DSP_POWER_SAVER_UPPER_BOUND	0x06
#define BRGPHY_DSP_POWER_SAVER_LOWER_BOUND	0x07
#define BRGPHY_DSP_LAST_ECHO			0x08
#define BRGPHY_DSP_FREQUENCY			0x09
#define BRGPHY_DSP_PLL_BANDWIDTH		0x0A
#define BRGPHY_DSP_PLL_PHASE_OFFSET		0x0B

#define BRGPHYDSP_FILTER_DCOFFSET		0x0C00
#define BRGPHY_DSP_FILTER_FEXT3			0x0B00
#define BRGPHY_DSP_FILTER_FEXT2			0x0A00
#define BRGPHY_DSP_FILTER_FEXT1			0x0900
#define BRGPHY_DSP_FILTER_FEXT0			0x0800
#define BRGPHY_DSP_FILTER_NEXT3			0x0700
#define BRGPHY_DSP_FILTER_NEXT2			0x0600
#define BRGPHY_DSP_FILTER_NEXT1			0x0500
#define BRGPHY_DSP_FILTER_NEXT0			0x0400
#define BRGPHY_DSP_FILTER_ECHO			0x0300
#define BRGPHY_DSP_FILTER_DFE			0x0200
#define BRGPHY_DSP_FILTER_FFE			0x0100

#define BRGPHY_DSP_CONTROL_ALL_FILTERS		0x1000

#define BRGPHY_DSP_SEL_CH_0			0x0000
#define BRGPHY_DSP_SEL_CH_1			0x2000
#define BRGPHY_DSP_SEL_CH_2			0x4000
#define BRGPHY_DSP_SEL_CH_3			0x6000

#define BRGPHY_MII_AUXCTL	0x18	/* AUX control */
#define BRGPHY_AUXCTL_LOW_SQ	0x8000	/* Low squelch */
#define BRGPHY_AUXCTL_LONG_PKT	0x4000	/* RX long packets */
#define BRGPHY_AUXCTL_ER_CTL	0x3000	/* Edgerate control */
#define BRGPHY_AUXCTL_TX_TST	0x0400	/* TX test, always 1 */
#define BRGPHY_AUXCTL_DIS_PRF	0x0080	/* dis part resp filter */
#define BRGPHY_AUXCTL_DIAG_MODE	0x0004	/* Diagnostic mode */

#define BRGPHY_MII_AUXSTS	0x19	/* AUX status */
#define BRGPHY_AUXSTS_ACOMP	0x8000	/* autoneg complete */
#define BRGPHY_AUXSTS_AN_ACK	0x4000	/* autoneg complete ack */
#define BRGPHY_AUXSTS_AN_ACK_D	0x2000	/* autoneg complete ack detect */
#define BRGPHY_AUXSTS_AN_NPW	0x1000	/* autoneg next page wait */
#define BRGPHY_AUXSTS_AN_RES	0x0700	/* AN HDC */
#define BRGPHY_AUXSTS_PDF	0x0080	/* Parallel detect. fault */
#define BRGPHY_AUXSTS_RF	0x0040	/* remote fault */
#define BRGPHY_AUXSTS_ANP_R	0x0020	/* AN page received */
#define BRGPHY_AUXSTS_LP_ANAB	0x0010	/* LP AN ability */
#define BRGPHY_AUXSTS_LP_NPAB	0x0008	/* LP Next page ability */
#define BRGPHY_AUXSTS_LINK	0x0004	/* Link status */
#define BRGPHY_AUXSTS_PRR	0x0002	/* Pause resolution-RX */
#define BRGPHY_AUXSTS_PRT	0x0001	/* Pause resolution-TX */

#define BRGPHY_RES_1000FD	0x0700	/* 1000baseT full duplex */
#define BRGPHY_RES_1000HD	0x0600	/* 1000baseT half duplex */
#define BRGPHY_RES_100FD	0x0500	/* 100baseT full duplex */
#define BRGPHY_RES_100T4	0x0400	/* 100baseT4 */
#define BRGPHY_RES_100HD	0x0300	/* 100baseT half duplex */
#define BRGPHY_RES_10FD		0x0200	/* 10baseT full duplex */
#define BRGPHY_RES_10HD		0x0100	/* 10baseT half duplex */

#define BRGPHY_MII_ISR		0x1A	/* interrupt status */
#define BRGPHY_ISR_PSERR	0x4000	/* Pair swap error */
#define BRGPHY_ISR_MDXI_SC	0x2000	/* MDIX Status Change */
#define BRGPHY_ISR_HCT		0x1000	/* counter above 32K */
#define BRGPHY_ISR_LCT		0x0800	/* all counter below 128 */
#define BRGPHY_ISR_AN_PR	0x0400	/* Autoneg page received */
#define BRGPHY_ISR_NO_HDCL	0x0200	/* No HCD Link */
#define BRGPHY_ISR_NO_HDC	0x0100	/* No HCD */
#define BRGPHY_ISR_USHDC	0x0080	/* Negotiated Unsupported HCD */
#define BRGPHY_ISR_SCR_S_ERR	0x0040	/* Scrambler sync error */
#define BRGPHY_ISR_RRS_CHG	0x0020	/* Remote RX status change */
#define BRGPHY_ISR_LRS_CHG	0x0010	/* Local RX status change */
#define BRGPHY_ISR_DUP_CHG	0x0008	/* Duplex mode change */
#define BRGPHY_ISR_LSP_CHG	0x0004	/* Link speed changed */
#define BRGPHY_ISR_LNK_CHG	0x0002	/* Link status change */
#define BRGPHY_ISR_CRCERR	0x0001	/* CEC error */

#define BRGPHY_MII_IMR		0x1B	/* interrupt mask */
#define BRGPHY_IMR_PSERR	0x4000	/* Pair swap error */
#define BRGPHY_IMR_MDXI_SC	0x2000	/* MDIX Status Change */
#define BRGPHY_IMR_HCT		0x1000	/* counter above 32K */
#define BRGPHY_IMR_LCT		0x0800	/* all counter below 128 */
#define BRGPHY_IMR_AN_PR	0x0400	/* Autoneg page received */
#define BRGPHY_IMR_NO_HDCL	0x0200	/* No HCD Link */
#define BRGPHY_IMR_NO_HDC	0x0100	/* No HCD */
#define BRGPHY_IMR_USHDC	0x0080	/* Negotiated Unsupported HCD */
#define BRGPHY_IMR_SCR_S_ERR	0x0040	/* Scrambler sync error */
#define BRGPHY_IMR_RRS_CHG	0x0020	/* Remote RX status change */
#define BRGPHY_IMR_LRS_CHG	0x0010	/* Local RX status change */
#define BRGPHY_IMR_DUP_CHG	0x0008	/* Duplex mode change */
#define BRGPHY_IMR_LSP_CHG	0x0004	/* Link speed changed */
#define BRGPHY_IMR_LNK_CHG	0x0002	/* Link status change */
#define BRGPHY_IMR_CRCERR	0x0001	/* CEC error */

#define BRGPHY_INTRS	\
	~(BRGPHY_IMR_LNK_CHG|BRGPHY_IMR_LSP_CHG|BRGPHY_IMR_DUP_CHG)

#define	MII_OUI_BROADCOM 0x001018
#define	MII_MODEL_BROADCOM_BCM5400	0x0004
#define	MII_MODEL_BROADCOM_BCM5401	0x0005
#define	MII_MODEL_BROADCOM_BCM5411	0x0007
#define	MII_MODEL_BROADCOM_BCM5421	0x000e
#define	MII_MODEL_BROADCOM_BCM5701	0x0011
#define	MII_MODEL_BROADCOM_BCM5703	0x0016
#define	MII_MODEL_BROADCOM_BCM5704	0x0019
#define	MII_MODEL_BROADCOM_BCM5705	0x001a
#define	MII_MODEL_BROADCOM_BCM5228	0x001c
#define	MII_MODEL_BROADCOM_BCM5248	0x001d
#define	MII_MODEL_BROADCOM_BCM5750	0x0018

static clib_error_t * brgphy_init (ethernet_phy_t * phy)
{
  clib_error_t * error = 0;
  u32 v;

  /* Make sure transmitter is enabled. */
  if ((error = ethernet_phy_read (phy, BRGPHY_EXTCTL, &v)))
    return error;

  if (v & BRGPHY_EXTCTL_DISABLE_TX)
    {
      v &= ~BRGPHY_EXTCTL_DISABLE_TX;
      error = ethernet_phy_write (phy, BRGPHY_EXTCTL, v);
    }

  /* Can't reset 5705 due to hardware bugs. */
  if (phy->device_id == MII_MODEL_BROADCOM_BCM5705)
    phy->device->reset = 0;

  return error;
}

/* Turn off tap power management on 5401. */
static ethernet_phy_reg_t dsp_5401[] = {
  { BRGPHY_MII_AUXCTL,		0x0c20 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x0012 },
  { BRGPHY_MII_DSP_RW_PORT,	0x1804 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x0013 },
  { BRGPHY_MII_DSP_RW_PORT,	0x1204 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x8006 },
  { BRGPHY_MII_DSP_RW_PORT,	0x0132 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x8006 },
  { BRGPHY_MII_DSP_RW_PORT,	0x0232 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x201f },
  { BRGPHY_MII_DSP_RW_PORT,	0x0a20 },
};

static ethernet_phy_reg_t dsp_5411[] = {
  { 0x1c,				0x8c23 },
  { 0x1c,				0x8ca3 },
  { 0x1c,				0x8c23 },
};

static ethernet_phy_reg_t dsp_5703[] = {
  { BRGPHY_MII_AUXCTL,		0x0c00 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x201f },
  { BRGPHY_MII_DSP_RW_PORT,	0x2aaa },
};

static ethernet_phy_reg_t dsp_5704[] = {
  { 0x1c,				0x8d68 },
  { 0x1c,				0x8d68 },
};

static ethernet_phy_reg_t dsp_5750[] = {
  { BRGPHY_MII_AUXCTL,		0x0c00 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x000a },
  { BRGPHY_MII_DSP_RW_PORT,	0x310b },
  { BRGPHY_MII_DSP_ADDR_REG,	0x201f },
  { BRGPHY_MII_DSP_RW_PORT,	0x9506 },
  { BRGPHY_MII_DSP_ADDR_REG,	0x401f },
  { BRGPHY_MII_DSP_RW_PORT,	0x14e2 },
  { BRGPHY_MII_AUXCTL,		0x0400 },
};

static clib_error_t *
set_wirespeed (ethernet_phy_t * phy)
{
  clib_error_t * e;
  u32 v;

  if ((e = ethernet_phy_write (phy, BRGPHY_MII_AUXCTL, 0x7007)))
    goto done;

  if ((e = ethernet_phy_read (phy, BRGPHY_MII_AUXCTL, &v)))
    goto done;

  v |= (1 << 4) | (1 << 15);

  if ((e = ethernet_phy_write (phy, BRGPHY_MII_AUXCTL, v)))
    goto done;

 done:
  return e;
}

static clib_error_t * brgphy_reset (ethernet_phy_t * phy)
{
  clib_error_t * error = 0;
  ethernet_phy_reg_t * regs;
  int n_regs;

  regs = 0;
  n_regs = 0;
  switch (phy->device_id)
    {
    case MII_MODEL_BROADCOM_BCM5400:
      regs = dsp_5401;
      n_regs = ARRAY_LEN (dsp_5401);
      break;

    case MII_MODEL_BROADCOM_BCM5401:
      if (phy->revision_id == 1 || phy->revision_id == 3)
	{
	  regs = dsp_5401;
	  n_regs = ARRAY_LEN (dsp_5401);
	}
      break;

    case MII_MODEL_BROADCOM_BCM5411:
      regs = dsp_5411;
      n_regs = ARRAY_LEN (dsp_5411);
      break;

    case MII_MODEL_BROADCOM_BCM5703:
      regs = dsp_5703;
      n_regs = ARRAY_LEN (dsp_5703);
      break;

    case MII_MODEL_BROADCOM_BCM5704:
      regs = dsp_5704;
      n_regs = ARRAY_LEN (dsp_5704);
      break;

    case MII_MODEL_BROADCOM_BCM5750:
      regs = dsp_5750;
      n_regs = ARRAY_LEN (dsp_5750);
      break;
    }

  if (n_regs > 0)
    {
      error = ethernet_phy_write_multiple (phy, regs, n_regs);
      if (error)
	return error;
    }

  /* 52[24]8 are not GIGE phys. */
  if (phy->device_id != MII_MODEL_BROADCOM_BCM5228
      && phy->device_id != MII_MODEL_BROADCOM_BCM5248)
    error = set_wirespeed (phy);

  return error;
}

static REGISTER_ETHERNET_PHY_DEVICE (brgphy_phy_device) = {
  .init = brgphy_init,
  .reset = brgphy_reset,
  .supported_devices = {
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5400, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5401, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5411, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5421, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5701, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5703, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5704, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5705, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5750, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5228, },
    { .vendor_id = MII_OUI_BROADCOM,
      .device_id = MII_MODEL_BROADCOM_BCM5248, },
    { 0 },
  }
};

void ethernet_phy_bcm_reference (void) { }
