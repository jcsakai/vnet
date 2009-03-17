/*
 * pg_stream.c: packet generator streams
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

/* Mark stream active or inactive. */
void pg_stream_enable_disable (pg_main_t * pg, pg_stream_t * s, int want_enabled)
{
  want_enabled = want_enabled != 0;
  pg_interface_t * pi = vec_elt_at_index (pg->interfaces, s->pg_if_index);

  if (pg_stream_is_enabled (s) == want_enabled)
    /* No change necessary. */
    return;
      
  if (want_enabled)
    s->n_buffers_generated = 0;

  /* Toggle enabled flag. */
  s->flags ^= PG_STREAM_FLAGS_IS_ENABLED;

  ASSERT (! pool_is_free (pg->streams, s));

  pg->enabled_streams
    = clib_bitmap_set (pg->enabled_streams, s - pg->streams, want_enabled);

  vlib_hw_interface_set_flags (pg->vlib_main, pi->hw_if_index,
			       (want_enabled
				? VLIB_INTERFACE_FLAG_IS_UP
				: VLIB_INTERFACE_FLAG_IS_DOWN));

  vlib_node_enable_disable (pg->vlib_main,
			    pg_input_node.index,
			    ! clib_bitmap_is_zero (pg->enabled_streams));

  s->buffer_accumulator = 0;
  s->time_last_generate = 0;
}

static vlib_device_class_t pg_dev_class = {
  .name = "pg",
  .tx_function = pg_output,
};

static vlib_hw_interface_class_t pg_interface_class = {
  .name = "Packet generator",
  .interface_base_name = "pg",
};

u32 pg_interface_find_free (pg_main_t * pg)
{
  vlib_main_t * vm = pg->vlib_main;
  pg_interface_t * pi;
  vlib_hw_interface_t * hi;
  u32 i, l;

  if ((l = vec_len (pg->free_interfaces)) > 0)
    {
      i = pg->free_interfaces[l - 1];
      _vec_len (pg->free_interfaces) = l - 1;
    }    
  else
    {
      i = vec_len (pg->interfaces);
      vec_add2 (pg->interfaces, pi, 1);

      pi->hw_if_index = vlib_register_interface (vm,
						 &pg_dev_class, i,
						 &pg_interface_class, 0);
      hi = vlib_get_hw_interface (vm, pi->hw_if_index);
      pi->sw_if_index = hi->sw_if_index;
    }

  return i;
}

static void perform_fixed_edits (pg_stream_t * stream)
{
  pg_edit_t * e, * old_edits, * new_edits;
  u8 * s, * v, * m;

  old_edits = stream->edits;
  new_edits = 0;
  s = 0;
  m = 0;
  vec_foreach (e, old_edits)
    {
      u32 i, i0, i1, mask, n_bits_left, n_bytes;

      i0 = e->bit_offset / BITS (u8);
      i1 = e->bit_offset % BITS (u8);

      n_bytes = 0;
      n_bits_left = e->n_bits;
      if (i1 != 0 && n_bits_left > 0)
	{
	  u32 n = clib_min (n_bits_left, BITS (u8) - i1);
	  n_bytes++;
	  n_bits_left -= n;
	}
      
      n_bytes += n_bits_left / BITS (u8);
      n_bytes += (n_bits_left % BITS (u8)) != 0;

      /* Make space for edit in value and mask. */
      vec_validate (s, i0 + n_bytes - 1);
      vec_validate (m, i0 + n_bytes - 1);

      if (e->type != PG_EDIT_FIXED)
	{
	  switch (e->type)
	    {
	    case PG_EDIT_RANDOM:
	    case PG_EDIT_INCREMENT:
	      e->last_increment_value = pg_edit_get_value (e, PG_EDIT_LO);
	      break;

	    default:
	      break;
	    }

	  vec_add1 (new_edits, e[0]);
	  continue;
	}

      i = n_bytes - 1;
      i0 = (e->bit_offset + e->n_bits - 1) / BITS (u8);
      i1 = e->bit_offset % BITS (u8);
      n_bits_left = e->n_bits;
      v = e->values[PG_EDIT_LO];

      if (i1 != 0 && n_bits_left > 0)
	{
	  u32 n = clib_min (n_bits_left, BITS (u8) - i1);

	  mask = pow2_mask (n) << i1;

	  ASSERT (i0 < vec_len (s));
	  ASSERT (i < vec_len (v));
	  ASSERT ((v[i] &~ mask) == 0);

	  s[i0] |= v[i] & mask;
	  m[i0] |= mask;

	  i0--;
	  i--;
	  n_bits_left -= n;
	}

      while (n_bits_left >= 8)
	{
	  ASSERT (i0 < vec_len (s));
	  ASSERT (i < vec_len (v));

	  s[i0] = v[i];
	  m[i0] = ~0;

	  i0--;
	  i--;
	  n_bits_left -= 8;
	}

      if (n_bits_left > 0)
	{
	  mask = pow2_mask (n_bits_left);

	  ASSERT (i0 < vec_len (s));
	  ASSERT (i < vec_len (v));
	  ASSERT ((v[i] &~ mask) == 0);

	  s[i0] |= v[i] & mask;
	  m[i0] |= mask;
	}

      pg_edit_free (e);
    }

  vec_free (old_edits);
  stream->buffer_data = s;
  stream->buffer_data_mask = m;
  stream->edits = new_edits;
}

static void
compute_edit_bit_offsets (pg_stream_t * s)
{
  pg_edit_t * e;
  pg_edit_group_t * g;
  u32 o, i, j, j0, j1;

  for (i = 1; i < vec_len (s->edit_groups); i++)
    {
      g = s->edit_groups + i;

      j0 = g[-1].start_edit_index;
      j1 = g[0].start_edit_index;

      o = 0;
      for (j = j0; j < j1; j++)
	{
	  e = s->edits + j;
	  o = clib_max (o, e->bit_offset + e->n_bits);
	}
      g[0].start_bit_offset = o;

      /* Relocate all edits in this group by
	 bit offset of group. */
      j0 = j1;
      j1 = (g + 1 < vec_end (s->edit_groups)
	    ? g[1].start_edit_index
	    : vec_len (s->edits));
      for (j = j0; j < j1; j++)
	{
	  e = s->edits + j;
	  e->bit_offset += g[0].start_bit_offset;
	}
    }
}
	
void pg_stream_add (pg_main_t * pg, pg_stream_t * s_init)
{
  vlib_main_t * vm = pg->vlib_main;
  pg_stream_t * s;
  uword * p;

  if (! pg->stream_index_by_name)
    pg->stream_index_by_name
      = hash_create_vec (0, sizeof (s->name[0]), sizeof (uword));

  /* Delete any old stream with the same name. */
  if (s_init->name
      && (p = hash_get_mem (pg->stream_index_by_name, s_init->name)))
    {
      pg_stream_del (pg, p[0]);
    }

  pool_get (pg->streams, s);
  s[0] = s_init[0];

  /* Give it a name. */
  if (! s->name)
    s->name = format (0, "stream%d", s - pg->streams);
  else
    s->name = vec_dup (s->name);

  hash_set_mem (pg->stream_index_by_name, s->name, s - pg->streams);

  compute_edit_bit_offsets (s);

  /* Get fixed part of buffer data. */
  perform_fixed_edits (s);

  /* Determine buffer size. */
  switch (s->buffer_size_edit_type)
    {
    case PG_EDIT_INCREMENT:
    case PG_EDIT_RANDOM:
      break;

    default:
      /* Get buffer size from fixed edits. */
      s->buffer_size_edit_type = PG_EDIT_FIXED;
      s->min_buffer_bytes = s->max_buffer_bytes = vec_len (s->buffer_data);
      break;
    }

  s->last_increment_buffer_size = s->min_buffer_bytes;

  s->free_list_index = vlib_buffer_create_free_list (vm, s->max_buffer_bytes);

  /* Find an interface to use. */
  s->pg_if_index = pg_interface_find_free (pg);

  {
    pg_interface_t * pi = vec_elt_at_index (pg->interfaces, s->pg_if_index);
    vlib_rx_or_tx_t rx_or_tx;

    vlib_foreach_rx_tx (rx_or_tx)
    {
      if (s->sw_if_index[rx_or_tx] == ~0)
	s->sw_if_index[rx_or_tx] = pi->sw_if_index;
    }
  }

  /* Connect the graph. */
  s->next_index = vlib_node_add_next (vm, pg_input_node.index, s->node_index);
}

void pg_stream_del (pg_main_t * pg, uword index)
{
  vlib_main_t * vm = pg->vlib_main;
  pg_stream_t * s;

  s = pool_elt_at_index (pg->streams, index);

  pg_stream_enable_disable (pg, s, /* want_enabled */ 0);
  vec_add1 (pg->free_interfaces, s->pg_if_index);
  hash_unset_mem (pg->stream_index_by_name, s->name);

  vlib_buffer_delete_free_list (vm, s->free_list_index);
  clib_fifo_free (s->buffer_fifo);

  pg_stream_free (s);
  pool_put (pg->streams, s);
}

