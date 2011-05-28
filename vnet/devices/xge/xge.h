#ifndef included_xge_xge_h
#define included_xge_xge_h

#include <clib/format.h>

/* 10 GIG E (XGE) PHY IEEE 802.3 clause 45 definitions. */

#define XGE_PHY_CONTROL 0x0
#define XGE_PHY_CONTROL_RESET (1 << 15)
#define XGE_PHY_CONTROL_LOOPBACK (1 << 14)
#define XGE_PHY_CONTROL_POWERDOWN (1 << 11)

#define XGE_PHY_STATUS 0x1
#define XGE_PHY_STATUS_LOCAL_FAULT (1 << 7)
#define XGE_PHY_STATUS_LINK_UP (1 << 2)
#define XGE_PHY_STATUS_POWERDOWN_ABILITY (1 << 1)

#define XGE_PHY_ID1 0x2
#define XGE_PHY_ID2 0x3

#define XGE_PHY_SPEED_ABILITY 0x4

/* IEEE standard device types. */
#define XGE_PHY_DEV_TYPE_CLAUSE_22 0
#define XGE_PHY_DEV_TYPE_PMA_PMD 1
#define XGE_PHY_DEV_TYPE_WIS 2
#define XGE_PHY_DEV_TYPE_PCS 3
#define XGE_PHY_DEV_TYPE_PHY_XS 4
#define XGE_PHY_DEV_TYPE_DTE_XS 5
#define XGE_PHY_DEV_TYPE_VENDOR_1 30
#define XGE_PHY_DEV_TYPE_VENDOR_2 31

/* 2 16 bit bitmaps of which devices are present. */
#define XGE_PHY_DEV_TYPES_PRESENT1 0x5
#define XGE_PHY_DEV_TYPES_PRESENT2 0x6

#define XGE_PHY_CONTROL2 7

/* PMA/PMD */
#define XGE_PHY_PMD_CONTROL2_PMA_TYPE(t) ((t) << 0)
#define foreach_xge_phy_pmd_control2_pma_type	\
  _ (reserved) _ (EW) _ (LW) _ (SW)		\
  _ (LX4) _ (ER) _ (LR) _ (SR)

#define XGE_PHY_PMD_CONTROL2_PCS_TYPE(t) ((t) << 0)
#define foreach_xge_phy_pmd_control2_pcs_type	\
  _ (R) _ (X) _ (W)

#define XGE_PHY_STATUS2 0x8

#define XGE_PHY_PMD_TX_DISABLE 0x9

/* [4:1] 4 lane status, [0] global signal detect */
#define XGE_PHY_PMD_SIGNAL_DETECT 0xa
#define XGE_PHY_PMD_SIGNAL_DETECT_GLOBAL (1 << 0)
#define XGE_PHY_PMD_SIGNAL_DETECT_LANE_SHIFT 1

#define XGE_PHY_PACKAGE_ID1 0xe
#define XGE_PHY_PACKAGE_ID2 0xf

/* PCS specific. */
#define XGE_PHY_PCS_10G_BASE_X_STATUS 0x18

#define XGE_PHY_PCS_10G_BASE_R_STATUS 0x20
#define XGE_PHY_PCS_10G_BASE_R_STATUS_RX_LINK_STATUS (1 << 12)
#define XGE_PHY_PCS_10G_BASE_R_STATUS_HI_BIT_ERROR_RATE (1 << 1)
#define XGE_PHY_PCS_10G_BASE_R_STATUS_BLOCK_LOCK (1 << 0)

#define XGE_PHY_PCS_10G_BASE_R_STATUS2 0x21
#define XGE_PHY_PCS_10G_BASE_R_JITTER_TEST(ab,i) (0x22 + 4*(ab) + (i))
#define XGE_PHY_PCS_10G_BASE_R_JITTER_TEST_CONTROL 0x2a
#define XGE_PHY_PCS_10G_BASE_R_JITTER_TEST_ERROR_COUNT 0x2b

/* XS specific. */
#define XGE_PHY_XS_LANE_STATUS 0x18
#define XGE_PHY_XS_LANE_STATUS_TX_LANES_ALIGNED (1 << 12)
#define XGE_PHY_XS_LANE_STATUS_LANES_SYNCED_SHIFT 0

#define XGE_PHY_XS_TEST_CONTROL 0x19

/* Supported PHY XS chips and IDs. */
#define foreach_xge_phy_id			\
  _ (quake_qt2020, 0x43a400)			\
  _ (bcm870x, 0x206000)				\
  _ (aelurous_1001, 0x3400800)			\
  _ (bcm8011, 0x406000)

typedef enum {
#define _(f,x) XGE_PHY_ID_##f = x,
    foreach_xge_phy_id
#undef _
} xge_phy_id_t;

always_inline u32
xge_phy_id_oui (u32 id)
{ return id &~ 0x3ff; }

format_function_t format_xge_phy_id;

#endif /* included_xge_xge_h */
