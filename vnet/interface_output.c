/*
 * interface_output.c: interface output node
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

#include <vnet/vnet.h>

typedef struct {
  u32 sw_if_index;
  u8 data[64 - sizeof (u32)];
} interface_output_trace_t;

u8 * format_vnet_interface_output_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  vlib_node_t * node = va_arg (*va, vlib_node_t *);
  interface_output_trace_t * t = va_arg (*va, interface_output_trace_t *);
  vnet_main_t * vnm = &vnet_main;
  vnet_sw_interface_t * si;
  uword indent;

  si = vnet_get_sw_interface (vnm, t->sw_if_index);
  indent = format_get_indent (s);

  s = format (s, "%U\n%U%U",
	      format_vnet_sw_interface_name, vnm, si,
	      format_white_space, indent,
	      node->format_buffer ? node->format_buffer : format_hex_bytes,
	      t->data, sizeof (t->data));

  return s;
}

static void
vnet_interface_output_trace (vlib_main_t * vm,
			     vlib_node_runtime_t * node,
			     vlib_frame_t * frame,
			     uword n_buffers)
{
  u32 n_left, * from;

  n_left = n_buffers;
  from = vlib_frame_args (frame);
  
  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      interface_output_trace_t * t0, * t1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, from[2], LOAD);
      vlib_prefetch_buffer_with_index (vm, from[3], LOAD);

      bi0 = from[0];
      bi1 = from[1];

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->sw_if_index = vnet_buffer (b0)->sw_if_index[VLIB_TX];
	  memcpy (t0->data, vlib_buffer_get_current (b0),
		  sizeof (t0->data));
	}
      if (b1->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
	  t1->sw_if_index = vnet_buffer (b1)->sw_if_index[VLIB_TX];
	  memcpy (t1->data, vlib_buffer_get_current (b1),
		  sizeof (t1->data));
	}
      from += 2;
      n_left -= 2;
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      interface_output_trace_t * t0;

      bi0 = from[0];

      b0 = vlib_get_buffer (vm, bi0);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->sw_if_index = vnet_buffer (b0)->sw_if_index[VLIB_TX];
	  memcpy (t0->data, vlib_buffer_get_current (b0),
		  sizeof (t0->data));
	}
      from += 1;
      n_left -= 1;
    }
}

static never_inline u32
slow_path (vlib_main_t * vm,
	   u32 bi,
	   vlib_buffer_t * b,
	   u32 n_left_to_tx,
	   u32 * to_tx,
	   u32 * n_slow_bytes_result)
{
  /* We've already enqueued a single buffer. */
  u32 n_buffers = 0;
  u32 n_slow_bytes = 0;

  while (n_left_to_tx > 0)
    {
      to_tx[0] = bi;
      to_tx += 1;
      n_left_to_tx -= 1;
      n_buffers += 1;
      n_slow_bytes += b->current_length;

      /* Be grumpy about zero length buffers for benefit of
	 driver tx function. */
      ASSERT (b->current_length > 0);

      if (! (b->flags & VLIB_BUFFER_NEXT_PRESENT))
	break;

      bi = b->next_buffer;
      b = vlib_get_buffer (vm, bi);
    }

  /* Ran out of space in next frame trying to enqueue buffers? */
  if (b->flags & VLIB_BUFFER_NEXT_PRESENT)
    return 0;

  *n_slow_bytes_result = n_slow_bytes;
  return n_buffers;
}

/* Interface output function. */
uword
vnet_interface_output_node (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_output_runtime_t * rt = (void *) node->runtime_data;
  vnet_sw_interface_t * si;
  u32 n_left_to_tx, * from, * from_end, * to_tx;
  u32 n_bytes, n_buffers, n_packets;

  n_buffers = frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vnet_interface_output_trace (vm, node, frame, n_buffers);

  from = vlib_frame_args (frame);

  if (rt->is_deleted)
    return vlib_error_drop_buffers (vm, node,
				    from,
				    /* buffer stride */ 1,
				    n_buffers,
				    VNET_INTERFACE_OUTPUT_NEXT_DROP,
				    node->node_index,
				    VNET_INTERFACE_OUTPUT_ERROR_INTERFACE_DELETED);

  si = vnet_get_sw_interface (vnm, rt->sw_if_index);
  if (! (si->flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP))
    return vlib_error_drop_buffers (vm, node,
				    from,
				    /* buffer stride */ 1,
				    n_buffers,
				    VNET_INTERFACE_OUTPUT_NEXT_DROP,
				    node->node_index,
				    VNET_INTERFACE_OUTPUT_ERROR_INTERFACE_DOWN);

  from_end = from + n_buffers;

  /* Total byte count of all buffers. */
  n_bytes = 0;
  n_packets = 0;

  while (from < from_end)
    {
      /* Get new next frame since previous incomplete frame may have less
	 than VNET_FRAME_SIZE vectors in it. */
      vlib_get_new_next_frame (vm, node, VNET_INTERFACE_OUTPUT_NEXT_TX,
			       to_tx, n_left_to_tx);

      while (from + 4 <= from_end && n_left_to_tx >= 2)
	{
	  u32 bi0, bi1;
	  vlib_buffer_t * b0, * b1;

	  /* Prefetch next iteration. */
	  vlib_prefetch_buffer_with_index (vm, from[2], LOAD);
	  vlib_prefetch_buffer_with_index (vm, from[3], LOAD);

	  bi0 = from[0];
	  bi1 = from[1];
	  to_tx[0] = bi0;
	  to_tx[1] = bi1;
	  from += 2;
	  to_tx += 2;
	  n_left_to_tx -= 2;

	  b0 = vlib_get_buffer (vm, bi0);
	  b1 = vlib_get_buffer (vm, bi1);

	  /* Be grumpy about zero length buffers for benefit of
	     driver tx function. */
	  ASSERT (b0->current_length > 0);
	  ASSERT (b1->current_length > 0);

	  n_bytes += b0->current_length + b1->current_length;
	  n_packets += 2;

	  if (PREDICT_FALSE ((b0->flags | b1->flags) & VLIB_BUFFER_NEXT_PRESENT))
	    {
	      u32 n_buffers, n_slow_bytes, i;

	      /* Undo. */
	      from -= 2;
	      to_tx -= 2;
	      n_left_to_tx += 2;
	      n_bytes -= b0->current_length + b1->current_length;
	      n_packets -= 2;

	      /* Do slow path two times. */
	      for (i = 0; i < 2; i++)
		{
		  u32 bi = i ? bi1 : bi0;
		  vlib_buffer_t * b = i ? b1 : b0;

		  n_buffers = slow_path (vm, bi, b,
					 n_left_to_tx, to_tx, &n_slow_bytes);

		  /* Not enough room for single packet? */
		  if (n_buffers == 0)
		    goto put;

		  from += 1;
		  to_tx += n_buffers;
		  n_left_to_tx -= n_buffers;
		  n_bytes += n_slow_bytes;
		  n_packets += 1;
		}
	    }
	}

      while (from + 1 <= from_end && n_left_to_tx >= 1)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;

	  bi0 = from[0];
	  to_tx[0] = bi0;
	  from += 1;
	  to_tx += 1;
	  n_left_to_tx -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  /* Be grumpy about zero length buffers for benefit of
	     driver tx function. */
	  ASSERT (b0->current_length > 0);

	  n_bytes += b0->current_length;
	  n_packets += 1;

	  if (PREDICT_FALSE (b0->flags & VLIB_BUFFER_NEXT_PRESENT))
	    {
	      u32 n_buffers, n_slow_bytes;

	      /* Undo. */
	      from -= 1;
	      to_tx -= 1;
	      n_left_to_tx += 1;
	      n_bytes -= b0->current_length;
	      n_packets -= 1;

	      n_buffers = slow_path (vm, bi0, b0,
				     n_left_to_tx, to_tx, &n_slow_bytes);

	      /* Not enough room for single packet? */
	      if (n_buffers == 0)
		goto put;

	      from += 1;
	      to_tx += n_buffers;
	      n_left_to_tx -= n_buffers;
	      n_bytes += n_slow_bytes;
	      n_packets += 1;
	    }
	}

    put:
      vlib_put_next_frame (vm, node, VNET_INTERFACE_OUTPUT_NEXT_TX, n_left_to_tx);
    }

  /* Update interface stats. */
  {
    vnet_interface_main_t * im = &vnm->interface_main;

    vlib_increment_combined_counter (im->combined_sw_if_counters
				     + VNET_INTERFACE_COUNTER_TX,
				     rt->sw_if_index,
				     n_packets,
				     n_bytes);
  }

  return n_buffers;
}

/* Use buffer's sw_if_index[VNET_RX] to choose output interface. */
static uword
vnet_per_buffer_interface_output (vlib_main_t * vm,
				  vlib_node_runtime_t * node,
				  vlib_frame_t * frame)
{
  vnet_main_t * vnm = &vnet_main;
  u32 n_left_to_next, * from, * to_next;
  u32 n_left_from, next_index;

  n_left_from = frame->n_vectors;

  from = vlib_frame_args (frame);
  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1, next0, next1;
	  vlib_buffer_t * b0, * b1;
	  vnet_hw_interface_t * hi0, * hi1;

	  /* Prefetch next iteration. */
	  vlib_prefetch_buffer_with_index (vm, from[2], LOAD);
	  vlib_prefetch_buffer_with_index (vm, from[3], LOAD);

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

	  hi0 = vnet_get_sup_hw_interface (vnm, vnet_buffer (b0)->sw_if_index[VLIB_TX]);
	  hi1 = vnet_get_sup_hw_interface (vnm, vnet_buffer (b1)->sw_if_index[VLIB_TX]);

	  next0 = hi0->hw_if_index;
	  next1 = hi1->hw_if_index;

	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next, n_left_to_next,
					   bi0, bi1, next0, next1);
	}

      while (n_left_from > 0)
	{
	  u32 bi0, next0;
	  vlib_buffer_t * b0;
	  vnet_hw_interface_t * hi0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  hi0 = vnet_get_sup_hw_interface (vnm, vnet_buffer (b0)->sw_if_index[VLIB_TX]);

	  next0 = hi0->hw_if_index;

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

always_inline u32
counter_index (vlib_main_t * vm, vlib_error_t e)
{
  vlib_node_t * n;
  u32 ci, ni;

  ni = vlib_error_get_node (e);
  n = vlib_get_node (vm, ni);

  ci = vlib_error_get_code (e);
  ASSERT (ci < n->n_errors);

  ci += n->error_heap_index;

  return ci;
}

static u8 * format_vnet_error_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  vlib_error_t * e = va_arg (*va, vlib_error_t *);
  vlib_node_t * error_node;
  vlib_error_main_t * em = &vm->error_main;
  u32 i;

  error_node = vlib_get_node (vm, vlib_error_get_node (e[0]));
  i = counter_index (vm, e[0]);
  s = format (s, "%v: %s", error_node->name, em->error_strings_heap[i]);

  return s;
}

static void
trace_errors_with_buffers (vlib_main_t * vm,
			   vlib_node_runtime_t * node,
			   vlib_frame_t * frame)
{
  u32 n_left, * buffers;

  buffers = vlib_frame_vector_args (frame);
  n_left = frame->n_vectors;
  
  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      vlib_error_t * t0, * t1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, buffers[2], LOAD);
      vlib_prefetch_buffer_with_index (vm, buffers[3], LOAD);

      bi0 = buffers[0];
      bi1 = buffers[1];

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0[0] = b0->error;
	}
      if (b1->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
	  t1[0] = b1->error;
	}
      buffers += 2;
      n_left -= 2;
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      vlib_error_t * t0;

      bi0 = buffers[0];

      b0 = vlib_get_buffer (vm, bi0);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0[0] = b0->error;
	}
      buffers += 1;
      n_left -= 1;
    }
}

static u8 *
validate_error (vlib_main_t * vm, vlib_error_t * e, u32 index)
{
  uword node_index = vlib_error_get_node (e[0]);
  uword code = vlib_error_get_code (e[0]);
  vlib_node_t * n;

  if (node_index >= vec_len (vm->node_main.nodes))
    return format (0, "[%d], node index out of range 0x%x, error 0x%x",
		   index, node_index, e[0]);

  n = vlib_get_node (vm, node_index);
  if (code >= n->n_errors)
    return format (0, "[%d], code %d out of range for node %v",
		   index, code, n->name);

  return 0;
}

static u8 *
validate_error_frame (vlib_main_t * vm,
		      vlib_node_runtime_t * node,
		      vlib_frame_t * f)
{
  u32 * buffers = vlib_frame_args (f);
  vlib_buffer_t * b;
  u8 * msg;
  uword i;

  for (i = 0; i < f->n_vectors; i++)
    {
      b = vlib_get_buffer (vm, buffers[i]);
      msg = validate_error (vm, &b->error, i);
      if (msg)
	return msg;
    }

  msg = vlib_validate_buffers
    (vm, buffers, /* stride */ 1,
     f->n_vectors,
     VLIB_BUFFER_KNOWN_ALLOCATED,
     /* follow_buffer_next */ 1);

  return msg;
}

typedef enum {
  VNET_ERROR_DISPOSITION_DROP,
  VNET_ERROR_DISPOSITION_PUNT,
  VNET_ERROR_N_DISPOSITION,
} vnet_error_disposition_t;

always_inline void
do_packet (vlib_main_t * vm, vlib_error_t a)
{
  vlib_error_main_t * em = &vm->error_main;
  u32 i = counter_index (vm, a);
  em->counters[i] += 1;
  vlib_error_elog_count (vm, i, 1);
}
    
static_always_inline uword
process_drop_punt (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * frame,
		   vnet_error_disposition_t disposition)
{
  vnet_main_t * vnm = &vnet_main;
  vlib_error_main_t * em = &vm->error_main;
  u32 * buffers, * first_buffer;
  vlib_error_t current_error;
  u32 current_counter_index, n_errors_left;
  u32 current_sw_if_index, n_errors_current_sw_if_index;
  u64 current_counter;
  vlib_simple_counter_main_t * cm;

  static vlib_error_t memory[VNET_ERROR_N_DISPOSITION];
  static char memory_init[VNET_ERROR_N_DISPOSITION];

  buffers = vlib_frame_args (frame);
  first_buffer = buffers;

  {
    vlib_buffer_t * b = vlib_get_buffer (vm, first_buffer[0]);

    if (! memory_init[disposition])
      {
	memory_init[disposition] = 1;
	memory[disposition] = b->error;
      }

    current_sw_if_index = vnet_buffer (b)->sw_if_index[VLIB_RX];
    n_errors_current_sw_if_index = 0;
  }

  current_error = memory[disposition];
  current_counter_index = counter_index (vm, memory[disposition]);
  current_counter = em->counters[current_counter_index];

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    trace_errors_with_buffers (vm, node, frame);
  
  n_errors_left = frame->n_vectors;
  cm = vec_elt_at_index (vnm->interface_main.sw_if_counters,
			 (disposition == VNET_ERROR_DISPOSITION_PUNT
			  ? VNET_INTERFACE_COUNTER_PUNT
			  : VNET_INTERFACE_COUNTER_DROP));

  while (n_errors_left >= 2)
    {
      vlib_buffer_t * b0, * b1;
      vnet_sw_interface_t * sw_if0, * sw_if1;
      vlib_error_t e0, e1;
      u32 bi0, bi1;
      u32 sw_if_index0, sw_if_index1;

      bi0 = buffers[0];
      bi1 = buffers[1];

      buffers += 2;
      n_errors_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      e0 = b0->error;
      e1 = b1->error;

      sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
      sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

      /* Speculate that sw_if_index == sw_if_index[01]. */
      n_errors_current_sw_if_index += 2;

      /* Speculatively assume all 2 (node, code) pairs are equal
	 to current (node, code). */
      current_counter += 2;

      if (PREDICT_FALSE (e0 != current_error
			 || e1 != current_error
			 || sw_if_index0 != current_sw_if_index
			 || sw_if_index1 != current_sw_if_index))
	{
	  current_counter -= 2;
	  n_errors_current_sw_if_index -= 2;

	  vlib_increment_simple_counter (cm, sw_if_index0, 1);
	  vlib_increment_simple_counter (cm, sw_if_index1, 1);

	  /* Increment super-interface drop/punt counters for
	     sub-interfaces. */
	  sw_if0 = vnet_get_sw_interface (vnm, sw_if_index0);
	  vlib_increment_simple_counter
	    (cm, sw_if0->sup_sw_if_index,
	     sw_if0->sup_sw_if_index != sw_if_index0);

	  sw_if1 = vnet_get_sw_interface (vnm, sw_if_index1);
	  vlib_increment_simple_counter
	    (cm, sw_if1->sup_sw_if_index, 
	     sw_if1->sup_sw_if_index != sw_if_index1);

	  em->counters[current_counter_index] = current_counter;
	  do_packet (vm, e0);
	  do_packet (vm, e1);

	  /* For 2 repeated errors, change current error. */
	  if (e0 == e1 && e1 != current_error)
	    {
	      current_error = e0;
	      current_counter_index = counter_index (vm, e0);
	    }
	  current_counter = em->counters[current_counter_index];
	}
    }

  while (n_errors_left >= 1)
    {
      vlib_buffer_t * b0;
      vnet_sw_interface_t * sw_if0;
      vlib_error_t e0;
      u32 bi0, sw_if_index0;

      bi0 = buffers[0];

      buffers += 1;
      n_errors_left -= 1;
      current_counter += 1;

      b0 = vlib_get_buffer (vm, bi0);
      e0 = b0->error;

      sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

      /* Increment drop/punt counters. */
      vlib_increment_simple_counter (cm, sw_if_index0, 1);

      /* Increment super-interface drop/punt counters for sub-interfaces. */
      sw_if0 = vnet_get_sw_interface (vnm, sw_if_index0);
      vlib_increment_simple_counter (cm, sw_if0->sup_sw_if_index, 
				     sw_if0->sup_sw_if_index != sw_if_index0);

      if (PREDICT_FALSE (e0 != current_error))
	{
	  current_counter -= 1;

	  vlib_error_elog_count (vm, current_counter_index,
				 (current_counter
				  - em->counters[current_counter_index]));
	    
	  em->counters[current_counter_index] = current_counter;

	  do_packet (vm, e0);
	  current_error = e0;
	  current_counter_index = counter_index (vm, e0);
	  current_counter = em->counters[current_counter_index];
	}
    }

  if (n_errors_current_sw_if_index > 0)
    {
      vnet_sw_interface_t * si;

      vlib_increment_simple_counter (cm, current_sw_if_index,
				     n_errors_current_sw_if_index);

      si = vnet_get_sw_interface (vnm, current_sw_if_index);
      if (si->sup_sw_if_index != current_sw_if_index)
	vlib_increment_simple_counter (cm, si->sup_sw_if_index,
				       n_errors_current_sw_if_index);
    }

  vlib_error_elog_count (vm, current_counter_index,
			 (current_counter
			  - em->counters[current_counter_index]));

  /* Return cached counter. */
  em->counters[current_counter_index] = current_counter;

  /* Save memory for next iteration. */
  memory[disposition] = current_error;

  if (disposition == VNET_ERROR_DISPOSITION_DROP
      || ! vm->os_punt_frame)
    {
      vlib_buffer_free
	(vm,
	 first_buffer,
	 frame->n_vectors);

      /* If there is no punt function, free the frame as well. */
      if (disposition == VNET_ERROR_DISPOSITION_PUNT && ! vm->os_punt_frame)
	vlib_frame_free (vm, node, frame);
    }
  else
    vm->os_punt_frame (vm, node, frame);

  return frame->n_vectors;
}

static uword
process_drop (vlib_main_t * vm,
	      vlib_node_runtime_t * node,
	      vlib_frame_t * frame)
{
  return process_drop_punt (vm, node, frame, VNET_ERROR_DISPOSITION_DROP);
}

static uword
process_punt (vlib_main_t * vm,
	      vlib_node_runtime_t * node,
	      vlib_frame_t * frame)
{
  return process_drop_punt (vm, node, frame, VNET_ERROR_DISPOSITION_PUNT);
}

static VLIB_REGISTER_NODE (drop_buffers) = {
  .function = process_drop,
  .name = "error-drop",
  .flags = VLIB_NODE_FLAG_IS_DROP,
  .vector_size = sizeof (u32),
  .format_trace = format_vnet_error_trace,
  .validate_frame = validate_error_frame,
};

static VLIB_REGISTER_NODE (punt_buffers) = {
  .function = process_punt,
  .flags = (VLIB_NODE_FLAG_FRAME_NO_FREE_AFTER_DISPATCH
	    | VLIB_NODE_FLAG_IS_PUNT),
  .name = "error-punt",
  .vector_size = sizeof (u32),
  .format_trace = format_vnet_error_trace,
  .validate_frame = validate_error_frame,
};

static clib_error_t *
vnet_per_buffer_interface_output_hw_interface_add_del (vnet_main_t * vm,
						       u32 hw_if_index,
						       u32 is_create);

static VLIB_REGISTER_NODE (vnet_per_buffer_interface_output_node) = {
  .function = vnet_per_buffer_interface_output,
  .name = "interface-output",
  .vector_size = sizeof (u32),
};

static clib_error_t *
vnet_per_buffer_interface_output_hw_interface_add_del (vnet_main_t * vm,
						       u32 hw_if_index,
						       u32 is_create)
{
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, hw_if_index);
  u32 next_index;

  next_index = vlib_node_add_next_with_slot
    (vm->vlib_main, vnet_per_buffer_interface_output_node.index,
     hi->output_node_index,
     /* next_index */ hw_if_index);

  ASSERT (next_index == hw_if_index);

  return 0;
}

static VNET_HW_INTERFACE_ADD_DEL_FUNCTION (vnet_per_buffer_interface_output_hw_interface_add_del);
