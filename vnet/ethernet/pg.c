/*
 * ethernet_pg.c: packet generator ethernet interface
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
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>

typedef struct {
  pg_edit_t type;
  pg_edit_t src_address;
  pg_edit_t dst_address;
} pg_ethernet_header_t;

static inline void
pg_ethernet_header_init (pg_ethernet_header_t * e)
{
  pg_edit_init (&e->type, ethernet_header_t, type);
  pg_edit_init (&e->src_address, ethernet_header_t, src_address);
  pg_edit_init (&e->dst_address, ethernet_header_t, dst_address);
}

typedef struct {
  pg_edit_t type;
  pg_edit_t id;
  pg_edit_t cfi;
  pg_edit_t priority;
} pg_ethernet_vlan_header_t;

static inline void
pg_ethernet_vlan_header_init (pg_ethernet_vlan_header_t * v,
			      int vlan_index)
{
  ASSERT (vlan_index < ARRAY_LEN (((ethernet_max_header_t *) 0)->vlan));
  pg_edit_init (&v->type, ethernet_max_header_t, vlan[vlan_index].type);

  pg_edit_init_bitfield (&v->id, ethernet_max_header_t,
			 vlan[vlan_index].priority_cfi_and_id,
			 0, 12);
  pg_edit_init_bitfield (&v->cfi, ethernet_max_header_t,
			 vlan[vlan_index].priority_cfi_and_id,
			 12, 1);
  pg_edit_init_bitfield (&v->priority, ethernet_max_header_t,
			 vlan[vlan_index].priority_cfi_and_id,
			 13, 3);
}

uword
unformat_pg_ethernet_header (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_ethernet_header_t * e;
  pg_ethernet_vlan_header_t * v;
  pg_edit_t * ether_type_edit;
  u32 n_vlan, error, group_index;
  
  e = pg_create_edit_group (s, sizeof (e[0]), sizeof (ethernet_header_t),
			    &group_index);
  pg_ethernet_header_init (e);
  error = 1;

  if (! unformat (input, "%U: %U -> %U",
		  unformat_pg_edit,
		    unformat_ethernet_type_net_byte_order, &e->type,
		  unformat_pg_edit,
		    unformat_ethernet_address, &e->src_address,
		  unformat_pg_edit,
		    unformat_ethernet_address, &e->dst_address))
    goto done;

  n_vlan = 0;
  while (unformat (input, "vlan"))
    {
      v = pg_add_edits (s, sizeof (v[0]), sizeof (ethernet_vlan_header_t),
			group_index);
      pg_ethernet_vlan_header_init (v, n_vlan);

      if (! unformat_user (input, unformat_pg_edit,
			   unformat_pg_number, &v->id))
	goto done;

      if (! unformat (input, "priority %U", unformat_pg_edit,
		      unformat_pg_number, &v->priority))
	pg_edit_set_fixed (&v->priority, 0);

      if (! unformat (input, "cfi %U", unformat_pg_edit,
		      unformat_pg_number, &v->cfi))
	pg_edit_set_fixed (&v->cfi, 0);

      /* Too many vlans given. */
      if (n_vlan >= 2)
	goto done;

      n_vlan++;
    }

  /* Address of e may have changed due to vlan edits being added */
  e = pg_get_edit_group (s, group_index);
  v = (void *) (e + 1);

  /* Correct types for vlan packets. */
  ether_type_edit = &e->type;
  if (n_vlan > 0)
    {
      int i;

      ether_type_edit = &v[n_vlan - 1].type;
      pg_edit_copy_type_and_values (ether_type_edit, &e->type);
      pg_edit_set_fixed (&e->type, ETHERNET_TYPE_VLAN);

      for (i = 0; i < n_vlan - 1; i++)
	pg_edit_set_fixed (&v[i].type, ETHERNET_TYPE_VLAN);
    }

  {
    ethernet_main_t * em = &ethernet_main;
    ethernet_type_info_t * ti = 0;
    pg_node_t * pg_node = 0;

    if (ether_type_edit->type == PG_EDIT_FIXED)
      {
	u16 t = *(u16 *) ether_type_edit->values[PG_EDIT_LO];
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

  error = 0;

 done:
  if (error)
    pg_free_edit_group (s);
  return error == 0;
}

/* As above but inserts and computes ethernet CRC. */
uword
unformat_pg_ethernet_header_with_crc (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_edit_t * e;
  u32 eth_group_index, n_bytes_in_ethernet_frame, ok;

  eth_group_index = vec_len (s->edit_groups);

  /* Correct for presence of CRC; unformat_pg_payload would otherwise
     make the packet 4 bytes too long for cases where it is used. */ 
  s->max_packet_bytes -= sizeof (u32);
  s->min_packet_bytes -= sizeof (u32);

  ok = unformat_user (input, unformat_pg_ethernet_header, s);

  /* Correct for presence of CRC. */ 
  s->max_packet_bytes += sizeof (u32);
  s->min_packet_bytes += sizeof (u32);

  if (! ok)
    return 0;

  n_bytes_in_ethernet_frame = pg_edit_group_n_bytes (s, eth_group_index);

  e = pg_create_edit_group (s, sizeof (e[0]), sizeof (u32),
			    /* resulting_group_index not useful */ 0);

  e->lsb_bit_offset = n_bytes_in_ethernet_frame * BITS (u8);
  e->n_bits = BITS (u32);

  /* FIXME maybe we should actually compute the CRC. */
  pg_edit_set_fixed (e, 0);

  return 1;
}
