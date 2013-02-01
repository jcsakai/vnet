/*
 * gnet/pg.c: packet generator gnet interfaces
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

#include <vlib/vlib.h>
#include <vnet/pg/pg.h>
#include <vnet/gnet/gnet.h>
#include <vnet/ethernet/ethernet.h>

uword
unformat_pg_gnet_header (unformat_input_t * input, va_list * args)
{
  ASSERT (0);
  return 0;
}

#if 0
typedef struct {
  pg_edit_t ttl;
  pg_edit_t is_inner_ring;
  pg_edit_t mode;
  pg_edit_t priority;
  pg_edit_t parity;
  pg_edit_t type;
  pg_edit_t src_address;
  pg_edit_t dst_address;
} pg_gnet_header_t;

static inline void
pg_gnet_header_init (pg_gnet_header_t * e)
{
  pg_edit_init (&e->ttl, gnet_and_ethernet_header_t, gnet.ttl);
  pg_edit_init_bitfield (&e->is_inner_ring, gnet_and_ethernet_header_t,
			 gnet.as_u16,
			 7, 1);
  pg_edit_init_bitfield (&e->mode, gnet_and_ethernet_header_t,
			 gnet.as_u16,
			 4, 3);
  pg_edit_init_bitfield (&e->priority, gnet_and_ethernet_header_t,
			 gnet.as_u16,
			 1, 3);
  pg_edit_init_bitfield (&e->parity, gnet_and_ethernet_header_t,
			 gnet.as_u16,
			 0, 1);
  pg_edit_init (&e->type, gnet_and_ethernet_header_t, ethernet.type);
  pg_edit_init (&e->src_address, gnet_and_ethernet_header_t, ethernet.src_address);
  pg_edit_init (&e->dst_address, gnet_and_ethernet_header_t, ethernet.dst_address);
}

uword
unformat_pg_gnet_header (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_gnet_header_t * e;
  u32 error, group_index;
  
  e = pg_create_edit_group (s, sizeof (e[0]), sizeof (gnet_header_t),
			    &group_index);
  pg_gnet_header_init (e);

  error = 1;
  if (! unformat (input, "%U: %U -> %U",
		  unformat_pg_edit,
		    unformat_ethernet_type_net_byte_order, &e->type,
		  unformat_pg_edit,
		    unformat_ethernet_address, &e->src_address,
		  unformat_pg_edit,
		    unformat_ethernet_address, &e->dst_address))
    goto done;

  {
    gnet_header_t h;

    h.as_u16 = 0;
    h.mode = GNET_MODE_data;
    h.ttl = 255;
    h.parity = count_set_bits (h.as_u16) ^ 1;
  
    pg_edit_set_fixed (&e->mode, h.mode);
    pg_edit_set_fixed (&e->ttl, h.ttl);
    pg_edit_set_fixed (&e->is_inner_ring, h.is_inner_ring);
    pg_edit_set_fixed (&e->priority, h.priority);
    pg_edit_set_fixed (&e->parity, h.parity);
  }

  error = 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "mode %U",
		    unformat_pg_edit,
		    unformat_pg_number, &e->mode))
	;
      else if (unformat (input, "ttl %U",
			 unformat_pg_edit,
			 unformat_pg_number, &e->ttl))
	;
      else if (unformat (input, "priority %U",
			 unformat_pg_edit,
			 unformat_pg_number, &e->priority))
	;
      else
	break;
    }
      
  {
    ethernet_main_t * em = &ethernet_main;
    ethernet_type_info_t * ti = 0;
    pg_node_t * pg_node = 0;

    if (e->type.type == PG_EDIT_FIXED)
      {
	u16 t = *(u16 *) e->type.values[PG_EDIT_LO];
	ti = ethernet_get_type_info (em, clib_net_to_host_u16 (t));
	if (ti && ti->node_index != ~0)
	  pg_node = pg_get_node (ti->node_index);
      }

    if (pg_node && pg_node->unformat_edit
	&& unformat_user (input, pg_node->unformat_edit, s))
      ;
    else if (! unformat_user (input, unformat_pg_payload, s))
      goto done;
  }

 done:
  if (error)
    pg_free_edit_group (s);
  return error == 0;
}
#endif
