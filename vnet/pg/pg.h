/*
 * pg.h: VNET packet generator
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

#ifndef included_vlib_pg_h
#define included_vlib_pg_h

#include <vlib/vlib.h>		/* for VLIB_N_RX_TX */
#include <vnet/pg/edit.h>
#include <clib/fifo.h>		/* for buffer_fifo */

struct pg_main_t;
struct pg_stream_t;

typedef struct pg_edit_group_t {
  /* Edits in this group. */
  pg_edit_t * edits;

  /* Vector of non-fixed edits for this group. */
  pg_edit_t * non_fixed_edits;

  /* Fixed edits for this group. */
  u8 * fixed_packet_data;
  u8 * fixed_packet_data_mask;

  /* Byte offset where packet data begins. */
  u32 start_byte_offset;

  /* Number of packet bytes for this edit group. */
  u32 n_packet_bytes;

  /* Function to perform miscellaneous edits (e.g. set IP checksum, ...). */
  void (* edit_function) (struct pg_main_t * pg,
			  struct pg_stream_t * s,
			  struct pg_edit_group_t * g,
			  u32 * buffers,
			  u32 n_buffers);

  /* Opaque data for edit function's use. */
  uword edit_function_opaque;
} pg_edit_group_t;

/* Packets are made of multiple buffers chained together.
   This struct keeps track of data per-chain index. */
typedef struct {
  /* Vector of buffer edits for this stream and buffer index. */
  pg_edit_t * edits;

  /* Buffers pre-initialized with fixed buffer data for this stream. */
  u32 * buffer_fifo;

  /* Buffer free list for this buffer index in stream. */
  u32 free_list_index;
} pg_buffer_index_t;

typedef struct pg_stream_t {
  /* Stream name. */
  u8 * name;

  u32 flags;

  /* Stream is currently enabled. */
#define PG_STREAM_FLAGS_IS_ENABLED (1 << 0)
#define PG_STREAM_FLAGS_DISABLE_BUFFER_RECYCLE (1 << 1)

  /* Edit groups are created by each protocol level (e.g. ethernet,
     ip4, tcp, ...). */
  pg_edit_group_t * edit_groups;

  pg_edit_type_t packet_size_edit_type;

  /* Min/max packet size. */
  u32 min_packet_bytes, max_packet_bytes;

  /* Vector of non-fixed edits for this stream.
     All fixed edits are performed and placed into fixed_packet_data. */
  pg_edit_t * non_fixed_edits;

  /* Packet data with all fixed edits performed.
     All packets in stream are initialized according with this data.
     Mask specifies which bits of packet data are covered by fixed edits. */
  u8 * fixed_packet_data, * fixed_packet_data_mask;

  /* Size to use for buffers.  0 means use buffers big enough
     for max_packet_bytes. */
  u32 buffer_bytes;

  /* Last packet length if packet size edit type is increment. */
  u32 last_increment_packet_size;

  /* Index into main interface pool for this stream. */
  u32 pg_if_index;

  /* Interface used to mark packets for this stream.  May be different
     than hw/sw index from pg main interface pool.  They will be
     different if this stream is being used generate buffers as if
     they were received on a non-pg interface.  For example, suppose you
     are trying to test vlan code and you want to generate buffers that
     appear to come from an ethernet interface. */
  u32 sw_if_index[VLIB_N_RX_TX];

  /* Node where stream's buffers get put. */
  u32 node_index;

  /* Output next index to reach output node from stream input node. */
  u32 next_index;

  /* Number of packets currently generated. */
  u64 n_packets_generated;

  /* Stream is disabled when packet limit is reached.
     Zero means no packet limit. */
  u64 n_packets_limit;

  /* Rate for this stream in packets/second.
     Zero means unlimited rate. */
  f64 rate_packets_per_second;

  f64 time_last_generate;

  f64 packet_accumulator;

  pg_buffer_index_t * buffer_indices;

  u8 ** replay_packet_templates;
  u32 current_replay_packet_index;
} pg_stream_t;

always_inline void
pg_buffer_index_free (pg_buffer_index_t * bi)
{
  vec_free (bi->edits);
  clib_fifo_free (bi->buffer_fifo);
}

always_inline void
pg_edit_group_free (pg_edit_group_t * g)
{
  pg_edit_t * e;
  vec_foreach (e, g->edits)
    pg_edit_free (e);
  vec_free (g->edits);
  vec_free (g->fixed_packet_data);
  vec_free (g->fixed_packet_data_mask);
}

always_inline void
pg_stream_free (pg_stream_t * s)
{
  pg_edit_group_t * g;
  pg_edit_t * e;
  vec_foreach (e, s->non_fixed_edits)
    pg_edit_free (e);
  vec_free (s->non_fixed_edits);
  vec_foreach (g, s->edit_groups)
    pg_edit_group_free (g);
  vec_free (s->edit_groups);
  vec_free (s->fixed_packet_data);
  vec_free (s->fixed_packet_data_mask);
  vec_free (s->name);

  {
    pg_buffer_index_t * bi;
    vec_foreach (bi, s->buffer_indices)
      pg_buffer_index_free (bi);
    vec_free (s->buffer_indices);
  }
}

always_inline int
pg_stream_is_enabled (pg_stream_t * s)
{ return (s->flags & PG_STREAM_FLAGS_IS_ENABLED) != 0; }

always_inline pg_edit_group_t *
pg_stream_get_group (pg_stream_t * s, u32 group_index)
{ return vec_elt_at_index (s->edit_groups, group_index); }

always_inline void *
pg_create_edit_group (pg_stream_t * s,
		      int n_edit_bytes,
		      int n_packet_bytes,
		      u32 * group_index)
{
  pg_edit_group_t * g;
  int n_edits;

  vec_add2 (s->edit_groups, g, 1);
  if (group_index)
    *group_index = g - s->edit_groups;

  ASSERT (n_edit_bytes % sizeof (pg_edit_t) == 0);
  n_edits = n_edit_bytes / sizeof (pg_edit_t);
  vec_resize (g->edits, n_edits);

  g->n_packet_bytes = n_packet_bytes;

  return g->edits;
}

always_inline void *
pg_add_edits (pg_stream_t * s, int n_edit_bytes, int n_packet_bytes,
	      u32 group_index)
{
  pg_edit_group_t * g = pg_stream_get_group (s, group_index);
  pg_edit_t * e;
  int n_edits;
  ASSERT (n_edit_bytes % sizeof (pg_edit_t) == 0);
  n_edits = n_edit_bytes / sizeof (pg_edit_t);
  vec_add2 (g->edits, e, n_edits);
  g->n_packet_bytes += n_packet_bytes;
  return e;
}

always_inline void *
pg_get_edit_group (pg_stream_t * s, u32 group_index)
{
  pg_edit_group_t * g = pg_stream_get_group (s, group_index);
  return g->edits;
}

/* Number of bytes for all groups >= given group. */
always_inline uword
pg_edit_group_n_bytes (pg_stream_t * s, u32 group_index)
{
  pg_edit_group_t * g;
  uword n_bytes = 0;

  for (g = s->edit_groups + group_index; g < vec_end (s->edit_groups); g++)
    n_bytes += g->n_packet_bytes;
  return n_bytes;
}

always_inline void
pg_free_edit_group (pg_stream_t * s)
{
  uword i = vec_len (s->edit_groups) - 1;
  pg_edit_group_t * g = pg_stream_get_group (s, i);

  pg_edit_group_free (g);
  memset (g, 0, sizeof (g[0]));
  _vec_len (s->edit_groups) = i;
}

typedef struct {
  /* VLIB interface indices. */
  u32 hw_if_index, sw_if_index;

  /* Identifies stream for this interface. */
  u32 stream_index;
} pg_interface_t;

/* Per VLIB node data. */
typedef struct {
  /* Parser function indexed by node index. */
  unformat_function_t * unformat_edit;
} pg_node_t;

typedef struct pg_main_t {
  /* Back pointer to main structure. */
  vlib_main_t * vlib_main;

  /* Pool of streams. */
  pg_stream_t * streams;

  /* Bitmap indicating which streams are currently enabled. */
  uword * enabled_streams;

  /* Hash mapping name -> stream index. */
  uword * stream_index_by_name;

  /* Vector of interfaces. */
  pg_interface_t * interfaces;

  /* Per VLIB node information. */
  pg_node_t * nodes;

  u32 * free_interfaces;
} pg_main_t;

/* Global main structure. */
extern pg_main_t pg_main;

/* Global node. */
extern vlib_node_registration_t pg_input_node;

/* Buffer generator input, output node functions. */
vlib_node_function_t pg_input, pg_output;

/* Stream add/delete. */
void pg_stream_del (pg_main_t * pg, uword index);
void pg_stream_add (pg_main_t * pg, pg_stream_t * s_init);

/* Enable/disable stream. */
void pg_stream_enable_disable (pg_main_t * pg, pg_stream_t * s, int is_enable);

/* Find/create free packet-generator interface index. */
u32 pg_interface_find_free (pg_main_t * pg, uword stream_index);

always_inline pg_node_t *
pg_get_node (uword node_index)
{
  pg_main_t * pg = &pg_main;
  vec_validate (pg->nodes, node_index);
  return pg->nodes + node_index;
}

void pg_edit_group_get_fixed_packet_data (pg_stream_t * s,
					  u32 group_index,
					  void * fixed_packet_data,
					  void * fixed_packet_data_mask);

#endif /* included_vlib_pg_h */
