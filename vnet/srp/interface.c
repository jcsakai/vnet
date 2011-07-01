/*
 * srp_interface.c: srp interfaces
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

#include <vlib/vlib.h>
#include <vnet/vnet/l3_types.h>
#include <vnet/pg/pg.h>
#include <vnet/srp/srp.h>

static uword srp_set_rewrite (vlib_main_t * vm,
			      u32 sw_if_index,
			      u32 l3_type,
			      void * rewrite,
			      uword max_rewrite_bytes)
{
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  srp_main_t * sm = &srp_main;
  srp_and_ethernet_header_t * h = rewrite;
  u16 type;
  uword n_bytes = sizeof (h[0]);

  if (n_bytes > max_rewrite_bytes)
    return 0;

  switch (l3_type) {
#define _(a,b) case VNET_L3_PACKET_TYPE_##a: type = ETHERNET_TYPE_##b; break
    _ (IP4, IP4);
    _ (IP6, IP6);
    _ (MPLS_UNICAST, MPLS_UNICAST);
    _ (MPLS_MULTICAST, MPLS_MULTICAST);
#undef _
  default:
    return 0;
  }

  memcpy (h->ethernet.src_address, hw->hw_address, sizeof (h->ethernet.src_address));
  memset (h->ethernet.dst_address, 0, sizeof (h->ethernet.dst_address));
  h->ethernet.type = clib_host_to_net_u16 (type);

  h->srp.as_u16 = 0;
  h->srp.mode = SRP_MODE_data;
  h->srp.ttl = sm->default_data_ttl;
  h->srp.parity = count_set_bits (h->srp.as_u16) ^ 1; /* odd parity */

  return n_bytes;
}

VLIB_HW_INTERFACE_CLASS (srp_hw_interface_class) = {
  .name = "SRP",
  .format_address = format_ethernet_address,
  .format_header = format_srp_header_with_length,
  .unformat_hw_address = unformat_ethernet_address,
  .unformat_header = unformat_srp_header,
  .set_rewrite = srp_set_rewrite,
};
