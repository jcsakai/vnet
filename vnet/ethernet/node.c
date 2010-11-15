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
  _ (DROP, "error-drop")			\
  _ (LLC, "llc-input")

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

static vlib_node_registration_t ethernet_input_node;

always_inline uword
ethernet_input_inline (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       vlib_frame_t * from_frame,
		       int is_ethernet_type)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  vlib_node_runtime_t * error_node;
  u32 n_left_from, next_index, * from, * to_next;

  if (is_ethernet_type)
    error_node = vlib_node_get_runtime (vm, ethernet_input_node.index);
  else
    error_node = node;

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (ethernet_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1;
	  vlib_buffer_t * b0, * b1;
	  ethernet_buffer_opaque_t * o0, * o1;
	  u32 i0, i1, type0, type1;
	  u8 next0, next1, enqueue_code;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * b2, * b3;

	    b2 = vlib_get_buffer (vm, from[2]);
	    b3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (b2, LOAD);
	    vlib_prefetch_buffer_header (b3, LOAD);

	    CLIB_PREFETCH (b2->data, sizeof (ethernet_header_t), LOAD);
	    CLIB_PREFETCH (b3->data, sizeof (ethernet_header_t), LOAD);
	  }

	  bi0 = from[0];
	  bi1 = from[1];
	  to_next[0] = bi0;
	  to_next[1] = bi1;
	  from += 2;
	  to_next += 2;
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  b0 = vlib_get_buffer (vm, bi0);
	  b1 = vlib_get_buffer (vm, bi1);

	  o0 = vlib_get_buffer_opaque (b0);
	  o1 = vlib_get_buffer_opaque (b1);

	  if (! is_ethernet_type)
	    {
	      ethernet_header_t * e0, * e1;

	      e0 = (void *) (b0->data + b0->current_data);
	      e1 = (void *) (b1->data + b1->current_data);

	      o0->start_of_ethernet_header = b0->current_data;
	      o1->start_of_ethernet_header = b1->current_data;

	      b0->current_data += sizeof (e0[0]);
	      b1->current_data += sizeof (e1[0]);

	      b0->current_length -= sizeof (e0[0]);
	      b1->current_length -= sizeof (e1[0]);

	      /* Index sparse array with network byte order. */
	      type0 = e0->type;
	      type1 = e1->type;
	    }
	  else
	    {
	      u16 * e0, * e1;

	      e0 = (void *) (b0->data + b0->current_data);
	      e1 = (void *) (b1->data + b1->current_data);

	      b0->current_data += sizeof (e0[0]);
	      b1->current_data += sizeof (e1[0]);

	      b0->current_length -= sizeof (e0[0]);
	      b1->current_length -= sizeof (e1[0]);

	      type0 = e0[0];
	      type1 = e1[0];
	    }

	  sparse_vec_index2 (em->input_next_by_type, type0, type1, &i0, &i1);
	  next0 = vec_elt (em->input_next_by_type, i0);
	  next1 = vec_elt (em->input_next_by_type, i1);

	  b0->error = error_node->errors[i0 == SPARSE_VEC_INVALID_INDEX ? ETHERNET_ERROR_UNKNOWN_TYPE : ETHERNET_ERROR_NONE];
	  b1->error = error_node->errors[i1 == SPARSE_VEC_INVALID_INDEX ? ETHERNET_ERROR_UNKNOWN_TYPE : ETHERNET_ERROR_NONE];

	  if (! is_ethernet_type)
	    {
	      u32 len0, len1;
	      u8 is_llc0, is_llc1;

	      len0 = clib_net_to_host_u16 (type0);
	      len1 = clib_net_to_host_u16 (type1);

	      is_llc0 = len0 < 0x600;
	      is_llc1 = len1 < 0x600;

	      next0 = is_llc0 ? ETHERNET_INPUT_NEXT_LLC : next0;
	      next1 = is_llc1 ? ETHERNET_INPUT_NEXT_LLC : next1;
	    }

	  enqueue_code = (next0 != next_index) + 2*(next1 != next_index);

	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      switch (enqueue_code)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = bi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next0, bi0);
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next1, bi1);
		  break;

		case 3:
		  /* A B B or A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  vlib_set_next_frame_buffer (vm, node, next0, bi0);
		  vlib_set_next_frame_buffer (vm, node, next1, bi1);

		  if (i0 == i1)
		    {
		      vlib_put_next_frame (vm, node, next_index,
					   n_left_to_next);
		      next_index = next1;
		      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
		    }
		}
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  ethernet_buffer_opaque_t * o0;
	  u32 i0, next0, type0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  o0 = vlib_get_buffer_opaque (b0);

	  if (! is_ethernet_type)
	    {
	      ethernet_header_t * e0;

	      e0 = (void *) (b0->data + b0->current_data);

	      o0->start_of_ethernet_header = b0->current_data;

	      b0->current_data += sizeof (e0[0]);

	      b0->current_length -= sizeof (e0[0]);

	      /* Index sparse array with network byte order. */
	      type0 = e0->type;
	    }
	  else
	    {
	      u16 * e0;

	      e0 = (void *) (b0->data + b0->current_data);

	      b0->current_data += sizeof (e0[0]);

	      b0->current_length -= sizeof (e0[0]);

	      type0 = e0[0];
	    }

	  i0 = sparse_vec_index (em->input_next_by_type, type0);
	  next0 = vec_elt (em->input_next_by_type, i0);

	  b0->error = error_node->errors[i0 == SPARSE_VEC_INVALID_INDEX ? ETHERNET_ERROR_UNKNOWN_TYPE : ETHERNET_ERROR_NONE];
	  
	  if (! is_ethernet_type)
	    {
	      u32 len0;
	      u8 is_llc0;

	      len0 = clib_net_to_host_u16 (type0);

	      is_llc0 = len0 < 0x600;

	      next0 = is_llc0 ? ETHERNET_INPUT_NEXT_LLC : next0;
	    }

	  /* Sent packet to wrong next? */
	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      /* Return old frame; remove incorrectly enqueued packet. */
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);

	      /* Send to correct next. */
	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static uword
ethernet_input (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * from_frame)
{ return ethernet_input_inline (vm, node, from_frame, /* is_ethernet_type */ 0); }

static uword
ethernet_type_input (vlib_main_t * vm,
		     vlib_node_runtime_t * node,
		     vlib_frame_t * from_frame)
{ return ethernet_input_inline (vm, node, from_frame, /* is_ethernet_type */ 1); }

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
    ((flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) ? sw_if_index : si->sup_sw_if_index);

 done:
  return error;
}

static char * ethernet_error_strings[] = {
#define ethernet_error(n,c,s) s,
#include "error.def"
#undef ethernet_error
};

static VLIB_REGISTER_NODE (ethernet_input_node) = {
  .function = ethernet_input,
  .name = "ethernet-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

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

  .sw_interface_admin_up_down_function = ethernet_sw_interface_up_down,
};

static VLIB_REGISTER_NODE (ethernet_type_input_node) = {
  .function = ethernet_type_input,
  .name = "ethernet-type-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_next_nodes = ETHERNET_INPUT_N_NEXT,
  .next_nodes = {
#define _(s,n) [ETHERNET_INPUT_NEXT_##s] = n,
    foreach_ethernet_input_next
#undef _
  },

  .format_buffer = format_ethernet_header_with_length,
  .format_trace = format_ethernet_input_trace,
  .unformat_buffer = unformat_ethernet_header,
};

static clib_error_t * ethernet_input_init (vlib_main_t * vm)
{
  ethernet_main_t * em = ethernet_get_main (vm);

  ethernet_setup_node (vm, ethernet_input_node.index);
  ethernet_setup_node (vm, ethernet_type_input_node.index);

  em->input_next_by_type = sparse_vec_new
    (/* elt bytes */ sizeof (em->input_next_by_type[0]),
     /* bits in index */ BITS (((ethernet_header_t *) 0)->type));

  vec_validate (em->sparse_index_by_input_next_index, ETHERNET_INPUT_NEXT_DROP);
  vec_validate (em->sparse_index_by_input_next_index, ETHERNET_INPUT_NEXT_PUNT);
  em->sparse_index_by_input_next_index[ETHERNET_INPUT_NEXT_DROP]
    = SPARSE_VEC_INVALID_INDEX;
  em->sparse_index_by_input_next_index[ETHERNET_INPUT_NEXT_PUNT]
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
  u16 * n;
  u32 i;

  {
    clib_error_t * error = vlib_call_init_function (vm, ethernet_input_init);
    if (error)
      clib_error_report (error);
  }

  ti->node_index = node_index;
  ti->next_index = vlib_node_add_next (vm, 
				       ethernet_input_node.index,
				       node_index);
  i = vlib_node_add_next (vm, 
			  ethernet_type_input_node.index,
			  node_index);
  ASSERT (i == ti->next_index);

  /* Setup ethernet type -> next index sparse vector mapping. */
  n = sparse_vec_validate (em->input_next_by_type, clib_host_to_net_u16 (type));
  n[0] = ti->next_index;

  /* Rebuild next index -> sparse index inverse mapping when sparse vector
     is updated. */
  vec_validate (em->sparse_index_by_input_next_index, ti->next_index);
  for (i = 1; i < vec_len (em->input_next_by_type); i++)
    em->sparse_index_by_input_next_index[em->input_next_by_type[i]] = i;
}
