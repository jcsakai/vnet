/*
 * MPLS packet format
 *
 * Copyright (c) 2012 Eliot Dresselhaus
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

#ifndef included_vnet_mpls_packet_h
#define included_vnet_mpls_packet_h

typedef struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
  u32 label : 20;

  u32 traffic_class : 3;

  u32 is_final_label : 1;

  u32 ttl : 8;
#else
  u32 ttl : 8;

  u32 is_final_label : 1;

  u32 traffic_class : 3;

  u32 label : 20;
#endif
} mpls_header_t;

#define foreach_mpls_special_label		\
  _ (ip4_explicit_null, 0)			\
  _ (router_alert, 1)				\
  _ (ip6_explicit_null, 2)			\
  _ (implicit_null, 3)				\
  _ (gal_label, 13)				\
  _ (oam_alert, 14)

typedef enum {
#define _(f,n) MPLS_LABEL_##f = n,
  foreach_mpls_special_label
#undef _
  MPLS_N_RESERVED_LABELS = 16,
} mpls_reserved_label_t;

typedef union {
  mpls_header_t as_mpls;
  u32 as_u32;
} mpls_header_union_t;

#endif /* included_vnet_mpls_packet_h */
