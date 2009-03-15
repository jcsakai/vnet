/*
 * ethernet_node.c: ethernet packet processing
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
#include <clib/sparse_vec.h>

#define foreach_ethernet_input_next		\
  _ (PUNT, "error-punt")			\
  _ (DROP, "error-drop")

typedef enum {
#define _(s,n) ETHERNET_INPUT_NEXT_##s,
  foreach_ethernet_input_next
#undef _
  ETHERNET_INPUT_N_NEXT,
} ethernet_input_next_t;

typedef struct {
  u8 packet_data[32];
} ethernet_input_trace_t;

static u8 * format_ethernet_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ethernet_input_trace_t * t = va_arg (*va, ethernet_input_trace_t *);

  s = format (s, "%U", format_ethernet_header, t->packet_data);

  return s;
}

typedef struct {
  /* Sparse vector mapping ethernet type in network byte order
     to next index. */
  u16 * next_by_type;

  u32 * sparse_index_by_next_index;
} ethernet_input_runtime_t;

#if 0
static uword
ethernet_input (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame,
		uword n_packets)
{
  ethernet_input_runtime_t * rt = (void *) node->runtime_data;
  vlib_error_meta_data_t unknown_type_error;
  u32 n_left_from, next_index, i_next, * from, * to_next;
  sparse_vec_header_t * next_by_type = sparse_vec_header (rt->next_by_type);

  unknown_type_error.node = node->node_index;
  unknown_type_error.code = ETHERNET_ERROR_UNKNOWN_TYPE;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_packet_data (vm, node, frame, n_packets,
			    sizeof (ethernet_input_trace_t));

  from = frame->packets;
  n_left_from = n_packets;
  next_index = node->last_next_index;
  i_next = vec_elt (rt->sparse_index_by_next_index, next_index);

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   &n_left_to_next, &to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 pi0, pi1;
	  vlib_packet_t * p0, * p1;
	  ethernet_header_t * e0, * e1;
	  u32 i0, i1, type0, type1, enqueue_code;

	  /* Prefetch next iteration. */
	  {
	    vlib_packet_t * p2, * p3;

	    p2 = vlib_get_packet (vm, from[2]);
	    p3 = vlib_get_packet (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->packet_data, sizeof (e0[0]), LOAD);
	    CLIB_PREFETCH (p3->packet_data, sizeof (e1[0]), LOAD);
	  }

	  pi0 = from[0];
	  pi1 = from[1];
	  to_next[0] = pi0;
	  to_next[1] = pi1;
	  from += 2;
	  to_next += 2;
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  p0 = vlib_get_packet (vm, pi0);
	  p1 = vlib_get_packet (vm, pi1);

	  e0 = (void *) (p0->packet_data + p0->current_header);
	  e1 = (void *) (p1->packet_data + p1->current_header);

	  p0->current_header += sizeof (e0[0]);
	  p1->current_header += sizeof (e1[0]);

	  p0->current_length -= sizeof (e0[0]);
	  p1->current_length -= sizeof (e1[0]);

	  /* Index sparse array with network byte order. */
	  type0 = e0->type;
	  type1 = e1->type;
	  sparse_vec_index2 (next_by_type, type0, type1, &i0, &i1);

	  enqueue_code = (i0 != i_next) + 2*(i1 != i_next);

	  if (PREDICT_FALSE (enqueue_code != 0 || ((i0 & i1) == 0)))
	    {
	      vlib_error_meta_data_t * e;

	      switch (enqueue_code)
		{
		case 0:
		  ASSERT (i0 + i1 + i_next == 0);
		  e = vlib_get_next_frame_meta_data (vm, node, next_index,
						   n_left_to_next + 2,
						   sizeof (e[0]));
		  e[0] = e[1] = unknown_type_error;
		  break;

		case 1:
		  /* A B A */
		  to_next[-2] = pi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  e = vlib_set_next_frame_meta (vm, node,
					      vec_elt (rt->next_by_type, i0),
					      pi0, sizeof (e[0]));
		  if (i0 == 0)
		    e[0] = unknown_type_error;
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  e = vlib_set_next_frame_meta (vm, node,
					      vec_elt (rt->next_by_type, i1),
					      pi1, sizeof (e[0]));
		  if (i1 == 0)
		    e[0] = unknown_type_error;
		  break;

		case 3:
		  to_next -= 2;
		  n_left_to_next += 2;
		  e = vlib_set_next_frame_meta (vm, node,
					      vec_elt (rt->next_by_type, i0),
					      pi0, sizeof (e[0]));
		  if (i0 == 0)
		    e[0] = unknown_type_error;

		  e = vlib_set_next_frame_meta (vm, node,
					      vec_elt (rt->next_by_type, i1),
					      pi1, sizeof (e[0]));
		  if (i1 == 0)
		    e[0] = unknown_type_error;

		  if (i0 == i1)
		    {
		      vlib_put_next_frame (vm, node, next_index,
					 n_left_to_next);
		      i_next = i1;
		      next_index = vec_elt (rt->next_by_type, i_next);
		      vlib_get_next_frame (vm, node, next_index,
					 &n_left_to_next, &to_next);
		    }
		}
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 pi0;
	  vlib_packet_t * p0;
	  ethernet_header_t * e0;
	  u32 i0, type0;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_packet (vm, pi0);

	  e0 = (void *) (p0->packet_data + p0->current_header);

	  p0->current_header += sizeof (e0[0]);
	  p0->current_length -= sizeof (e0[0]);

	  type0 = e0->type;
	  i0 = sparse_vec_index (rt->next_by_type, type0);
	  
	  if (PREDICT_FALSE ((i0 != i_next) || (i0 == 0)))
	    {
	      if (i0 != i_next)
		{
		  n_left_to_next += 1;
		  vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		  i_next = i0;
		  next_index = vec_elt (rt->next_by_type, i_next);
		  vlib_get_next_frame (vm, node, next_index,
				     &n_left_to_next, &to_next);

		  to_next[0] = pi0;
		  to_next += 1;
		  n_left_to_next -= 1;
		}

	      if (i0 == 0)
		{
		  vlib_error_meta_data_t * e;

		  e = vlib_get_next_frame_meta_data (vm, node, next_index,
						   n_left_to_next + 1,
						   sizeof (e[0]));
		  e[0] = unknown_type_error;
		}
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_packets;
}
#endif

static clib_error_t *
ethernet_sw_interface_up_down (vlib_main_t * vm,
			       u32 sw_if_index,
			       u32 flags)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  vlib_sw_interface_t * si;
  ethernet_vlan_mapping_t * m;
  clib_error_t * error = 0;

  si = vlib_get_sw_interface (vm, sw_if_index);
  if (si->type != VLIB_SW_INTERFACE_TYPE_SUB)
    goto done;

  m = vec_elt_at_index (em->vlan_mapping_by_sw_if_index,
			si->sup_sw_if_index);

  /* Sub-interface may not be ethernet. */
  if (! m)
    goto done;

  m->vlan_to_sw_if_index[si->sub.id] =
    ((flags & VLIB_INTERFACE_FLAG_IS_UP) ? sw_if_index : si->sup_sw_if_index);

 done:
  return error;
}

static char * ethernet_error_strings[] = {
#define ethernet_error(n,c,s) s,
#include "error.def"
#undef ethernet_error
};

VLIB_REGISTER_NODE (ethernet_input_node) = {
  .function = /* ethernet_input */ 0,
  .name = "ethernet-input",

  .runtime_data_bytes = sizeof (ethernet_input_runtime_t),

  .n_errors = ETHERNET_N_ERROR,
  .error_strings = ethernet_error_strings,

  .n_next_nodes = ETHERNET_INPUT_N_NEXT,
  .next_nodes = {
#define _(s,n) [ETHERNET_INPUT_NEXT_##s] = n,
    foreach_ethernet_input_next
#undef _
  },

  .format_buffer = format_ethernet_header_with_length,
  .format_trace = format_ethernet_input_trace,
  .unformat_buffer = unformat_ethernet_header,

  .sw_interface_up_down_function = ethernet_sw_interface_up_down,
};

static clib_error_t * ethernet_input_init (vlib_main_t * vm)
{
  ethernet_input_runtime_t * rt;

  rt = vlib_node_get_runtime_data (vm, ethernet_input_node.index);

  rt->next_by_type = sparse_vec_new
    (/* elt bytes */ sizeof (rt->next_by_type[0]),
     /* bits in index */ BITS (((ethernet_header_t *) 0)->type));

  vec_validate (rt->sparse_index_by_next_index, ETHERNET_INPUT_NEXT_DROP);
  vec_validate (rt->sparse_index_by_next_index, ETHERNET_INPUT_NEXT_PUNT);
  rt->sparse_index_by_next_index[ETHERNET_INPUT_NEXT_DROP]
    = SPARSE_VEC_INVALID_INDEX;
  rt->sparse_index_by_next_index[ETHERNET_INPUT_NEXT_PUNT]
    = SPARSE_VEC_INVALID_INDEX;

  return 0;
}

VLIB_INIT_FUNCTION (ethernet_input_init);

void
ethernet_register_input_type (vlib_main_t * vm,
			      ethernet_type_t type,
			      u32 node_index)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  ethernet_type_info_t * ti = ethernet_get_type_info (em, type);
  ethernet_input_runtime_t * rt;
  u16 * n;
  u32 i;

  ti->node_index = node_index;
  ti->next_index = vlib_node_add_next (vm, 
				     ethernet_input_node.index,
				     node_index);

  /* Setup ethernet type -> next index sparse vector mapping. */
  rt = vlib_node_get_runtime_data (vm, ethernet_input_node.index);
  n = sparse_vec_validate (rt->next_by_type, clib_host_to_net_u16 (type));
  n[0] = ti->next_index;

  /* Rebuild next index -> sparse index inverse mapping when sparse vector
     is updated. */
  vec_validate (rt->sparse_index_by_next_index, ti->next_index);
  for (i = 1; i < vec_len (rt->next_by_type); i++)
    rt->sparse_index_by_next_index[rt->next_by_type[i]] = i;
}
