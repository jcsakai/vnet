#include <vnet/ip/ip.h>
#include <math.h>

/* 20 byte TCP + 12 bytes of options (timestamps) = 32 bytes */
typedef struct {
  u64 sequence_number;
  f64 time_stamp;
  u32 stream_index;
  u32 is_ack;
  u32 unused[2];
} __attribute__ ((packed)) rtt_test_header_t;

typedef struct {
  ip4_header_t ip4;
  rtt_test_header_t rtt;
  u8 payload[0];
} __attribute__ ((packed)) rtt_test_packet_t;

typedef struct {
  ip4_address_t src_address, dst_address;

  f64 n_packets_to_send;

  f64 send_rate_bits_per_second;
  f64 send_rate_packets_per_second;

  f64 packet_accumulator;

  u64 n_packets_sent;
  u64 n_rx[3];

  f64 tx_times[2];

  u64 rx_expected_sequence_number;

  f64 sum_dt, sum_dt2;

  f64 rtt_histogram_bins_per_sec;

  u32 n_bytes_payload;

  /* Including IP & L2 header. */
  u32 n_bytes_per_packet_on_wire;

  u32 log2_n_histogram;

  u32 * rtt_histogram;

  f64 rx_ack_times[2];

  vlib_packet_template_t packet_template;
} rtt_test_stream_t;

typedef struct {
  /* Size of encapsulation (e.g. 14 for ethernet). */
  u32 n_encap_bytes;

  u32 is_sender;

  u32 verbose;

  u32 my_ip_protocol;

  f64 print_status_every_n_packets_sent;

  rtt_test_stream_t stream_history[32];
  u32 stream_history_index;

  rtt_test_stream_t * stream_pool;

  vlib_packet_template_t ack_packet_template;
  u16 ack_packet_template_ip4_checksum;
} rtt_test_main_t;

always_inline void
rtt_test_stream_free (vlib_main_t * vm, rtt_test_main_t * tm, rtt_test_stream_t * s)
{
  vlib_packet_template_free (vm, &s->packet_template);
  memset (&s->packet_template, 0, sizeof (s->packet_template));

  tm->stream_history[tm->stream_history_index++] = s[0];
  if (tm->stream_history_index >= ARRAY_LEN (tm->stream_history))
    tm->stream_history_index = 0;

  s->rtt_histogram = 0;
  pool_put (tm->stream_pool, s);
}

rtt_test_main_t rtt_test_main;

#define foreach_rtt_test_error				\
  _ (packets_received, "packets received")		\
  _ (listener_acks_dropped, "listener acks dropped")	\
  _ (unknown_stream, "unknown stream")

typedef enum {
#define _(sym,str) RTT_TEST_ERROR_##sym,
  foreach_rtt_test_error
#undef _
  RTT_TEST_N_ERROR,
} rtt_test_error_t;

static char * rtt_test_error_strings[] = {
#define _(sym,string) string,
  foreach_rtt_test_error
#undef _
};

static uword
handle_rx (rtt_test_main_t * tm, rtt_test_header_t * r0, f64 now)
{
  vlib_main_t * vm = &vlib_global_main;
  rtt_test_stream_t * s0;
  f64 dt0;
  u32 bin0, i0, out_of_seq0;

  s0 = pool_elt_at_index (tm->stream_pool, r0->stream_index);
  if (pool_is_free_index (tm->stream_pool, r0->stream_index))
    {
      ELOG_TYPE_DECLARE (e) = {
	.format = "rtt-test: unknown stream %d",
	.format_args = "i4",
      };
      struct { u32 stream; } * ed;
      ed = ELOG_DATA (&vm->elog_main, e);
      ed->stream = r0->stream_index;
      return 1;
    }

  dt0 = now - r0->time_stamp;

  i0 = r0->sequence_number == s0->rx_expected_sequence_number;
  i0 = (r0->sequence_number < s0->rx_expected_sequence_number
	? 0
	: (i0 ? 1 : 2));

  out_of_seq0 = i0 != 1;
  if (out_of_seq0)
    {
      ELOG_TYPE_DECLARE (e) = {
	.format = "rtt-test: out-of-seq expected %Ld got %Ld",
	.format_args = "i8i8",
      };
      struct { u64 expected, got; } * ed;
      ed = ELOG_DATA (&vm->elog_main, e);
      ed->expected = s0->rx_expected_sequence_number;
      ed->got = r0->sequence_number;
    }

  if (i0 == 2)
    s0->rx_expected_sequence_number = r0->sequence_number + 1;
  else if (i0 == 1)
    s0->rx_expected_sequence_number++;
  s0->n_rx[i0] += 1;

  s0->sum_dt += dt0;
  s0->sum_dt2 += dt0*dt0;

  bin0 = flt_round_nearest (dt0 * s0->rtt_histogram_bins_per_sec);

  ASSERT (is_pow2 (_vec_len (s0->rtt_histogram)));
  bin0 &= _vec_len (s0->rtt_histogram) - 1;

  s0->rtt_histogram[bin0] += 1;

  i0 = r0->sequence_number > 0;
  s0->rx_ack_times[i0] = now;
  return out_of_seq0;
}

typedef enum {
  RTT_TEST_RX_NEXT_DROP,
  RTT_TEST_RX_NEXT_ECHO,
  RTT_TEST_RX_N_NEXT,
} rtt_test_rx_next_t;

static uword
rtt_rx_listener (vlib_main_t * vm,
		 vlib_node_runtime_t * node,
		 vlib_frame_t * frame)
{
  rtt_test_main_t * tm = &rtt_test_main;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_drop, * to_echo;
  u32 n_left_from, n_left_to_drop, n_left_to_echo;
  f64 now = vlib_time_now (vm);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, RTT_TEST_RX_NEXT_DROP, to_drop, n_left_to_drop);
      vlib_get_next_frame (vm, node, RTT_TEST_RX_NEXT_ECHO, to_echo, n_left_to_echo);

      while (n_left_from > 0 && n_left_to_drop > 0 && n_left_to_echo > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  rtt_test_header_t * r0;
	  rtt_test_packet_t * ack0;
	  ip_csum_t sum0;
	  u32 bi0;
      
	  bi0 = to_drop[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_drop += 1;
	  n_left_to_drop -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);

	  r0 = ip4_next_header (ip0);

	  p0->error = node->errors[RTT_TEST_ERROR_listener_acks_dropped];

	  /* Don't ack acks. */
	  if (PREDICT_FALSE (r0->is_ack))
	    goto ack0;

	  ack0 = vlib_packet_template_get_packet (vm, &tm->ack_packet_template, to_echo);

	  to_echo += 1;
	  n_left_to_echo -= 1;

	  sum0 = tm->ack_packet_template_ip4_checksum;

	  ack0->ip4.src_address = ip0->dst_address;
	  sum0 = ip_csum_add_even (sum0, ack0->ip4.src_address.as_u32);

	  ack0->ip4.dst_address = ip0->src_address;
	  sum0 = ip_csum_add_even (sum0, ack0->ip4.dst_address.as_u32);

	  ack0->ip4.checksum = ip_csum_fold (sum0);

	  ASSERT (ack0->ip4.checksum == ip4_header_checksum (&ack0->ip4));

	  ack0->rtt = r0[0];
	  ack0->rtt.is_ack = 1;
	  continue;

	ack0:
	  handle_rx (tm, r0, now);
	}
  
      vlib_put_next_frame (vm, node, RTT_TEST_RX_NEXT_DROP, n_left_to_drop);
      vlib_put_next_frame (vm, node, RTT_TEST_RX_NEXT_ECHO, n_left_to_echo);
    }

  return frame->n_vectors;
}

static uword
rtt_rx_sender (vlib_main_t * vm,
	       vlib_node_runtime_t * node,
	       vlib_frame_t * frame)
{
  rtt_test_main_t * tm = &rtt_test_main;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  f64 now;

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = RTT_TEST_RX_NEXT_DROP;
  now = vlib_time_now (vm);
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  rtt_test_header_t * r0;
	  u32 bi0, error0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);
	  r0 = ip4_next_header (ip0);

	  error0 = (pool_is_free_index (tm->stream_pool, r0->stream_index)
		    ? RTT_TEST_ERROR_unknown_stream
		    : RTT_TEST_ERROR_packets_received);

	  handle_rx (tm, r0, now);

	  p0->error = node->errors[error0];
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  return frame->n_vectors;
}

static uword
rtt_test_rx (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  rtt_test_main_t * tm = &rtt_test_main;
  return (tm->is_sender ? rtt_rx_sender : rtt_rx_listener) (vm, node, frame);
}

VLIB_REGISTER_NODE (rtt_test_rx_node) = {
  .function = rtt_test_rx,
  .name = "rtt-test-rx",

  .vector_size = sizeof (u32),

  .n_next_nodes = RTT_TEST_RX_N_NEXT,
  .next_nodes = {
    [RTT_TEST_RX_NEXT_DROP] = "error-drop",
    [RTT_TEST_RX_NEXT_ECHO] = "ip4-input-no-checksum",
  },

  .n_errors = RTT_TEST_N_ERROR,
  .error_strings = rtt_test_error_strings,
};

always_inline void
rtt_test_tx_packets (vlib_main_t * vm,
		     vlib_node_runtime_t * node,
		     rtt_test_stream_t * s,
		     f64 time_now,
		     uword n_packets_to_send)
{
  u32 * to_next, n_this_frame, n_left, n_trace, next, i;
  rtt_test_packet_t * p;
  vlib_buffer_t * b;

  next = 0;
  while (n_packets_to_send > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left);

      n_this_frame = clib_min (n_packets_to_send, n_left);

      for (i = 0; i < n_this_frame; i++)
	{
	  p = vlib_packet_template_get_packet (vm, &s->packet_template, to_next + i);
	  p->rtt.is_ack = 0;
	  p->rtt.time_stamp = time_now;
	  p->rtt.sequence_number = s->n_packets_sent + i;
	}

      n_trace = vlib_get_trace_count (vm, node);
      if (n_trace > 0)
	{
	  u32 n = clib_min (n_trace, n_this_frame);

	  vlib_set_trace_count (vm, node, n_trace - n);
	  for (i = 0; i < n_this_frame; i++)
	    {
	      b = vlib_get_buffer (vm, to_next[i]);
	      vlib_trace_buffer (vm, node, next, b, /* follow_chain */ 1);
	    }
	}

      s->n_packets_sent += n_this_frame;
      n_packets_to_send -= n_this_frame;
      n_left -= n_this_frame;

      vlib_put_next_frame (vm, node, next, n_left);
    }
}

always_inline uword
rtt_test_stream_is_done (rtt_test_stream_t * s, f64 time_now)
{
  /* Need to send more packets? */
  if (s->n_packets_to_send > 0 && s->n_packets_sent < s->n_packets_to_send)
    return 0;

  /* Received everything we've sent? */
  if (s->n_rx[0] + s->n_rx[1] + s->n_rx[2] >= s->n_packets_to_send)
    return 1;

  /* No ACK received after 5 seconds of sending. */
  if (s->rx_ack_times[0] == 0
      && s->n_packets_sent > 0
      && time_now - s->tx_times[0] > 5)
    return 1;

  /* No ACK received after 5 seconds of waiting? */
  if (time_now - s->rx_ack_times[1] > 5)
    return 1;

  return 0;
}

always_inline uword
rtt_test_tx_stream (vlib_main_t * vm,
		    vlib_node_runtime_t * node,
		    rtt_test_stream_t * s)
{
  rtt_test_main_t * tm = &rtt_test_main;
  uword n_packets;
  f64 time_now, dt;

  time_now = vlib_time_now (vm);

  if (rtt_test_stream_is_done (s, time_now))
    {
      rtt_test_stream_free (vm, tm, s);
      if (pool_elts (tm->stream_pool) == 0)
	vlib_node_set_state (vm, node->node_index, VLIB_NODE_STATE_DISABLED);
      return 0;
    }

  /* Apply rate limit. */
  if (s->tx_times[1] == 0)
    s->tx_times[1] = time_now;

  dt = time_now - s->tx_times[1];
  s->tx_times[1] = time_now;

  n_packets = VLIB_FRAME_SIZE;
  if (s->send_rate_packets_per_second > 0)
    {
      s->packet_accumulator += dt * s->send_rate_packets_per_second;
      n_packets = s->packet_accumulator;

      /* Never allow accumulator to grow if we get behind. */
      s->packet_accumulator -= n_packets;
    }

  /* Apply fixed limit. */
  if (s->n_packets_to_send > 0
      && s->n_packets_sent + n_packets > s->n_packets_to_send)
    n_packets = s->n_packets_to_send - s->n_packets_sent;

  /* Generate up to one frame's worth of packets. */
  if (n_packets > VLIB_FRAME_SIZE)
    n_packets = VLIB_FRAME_SIZE;

  if (n_packets > 0)
    rtt_test_tx_packets (vm, node, s, time_now, n_packets);

  return n_packets;
}

static uword
rtt_test_tx (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  rtt_test_main_t * tm = &rtt_test_main;
  rtt_test_stream_t * s;
  uword n_packets = 0;

  pool_foreach (s, tm->stream_pool, ({
    n_packets += rtt_test_tx_stream (vm, node, s);
  }));

  return n_packets;
}

VLIB_REGISTER_NODE (rtt_test_tx_node) = {
  .function = rtt_test_tx,
  .name = "rtt-test-tx",
  .type = VLIB_NODE_TYPE_INPUT,
  .state = VLIB_NODE_STATE_DISABLED,

  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "ip4-input-no-checksum",
  },
};

static clib_error_t *
rtt_test_command (vlib_main_t * vm,
		  unformat_input_t * input,
		  vlib_cli_command_t * cmd)
{
  rtt_test_main_t * tm = &rtt_test_main;
  rtt_test_stream_t * s;

  pool_get (tm->stream_pool, s);

  memset (s, 0, sizeof (s[0]));
  s->n_packets_to_send = 1;
  s->send_rate_bits_per_second = 1e6;
  s->n_bytes_payload = 1448;
  s->log2_n_histogram = 14;
  s->rtt_histogram_bins_per_sec = 1e4;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "%U -> %U",
		    unformat_ip4_address, &s->src_address,
		    unformat_ip4_address, &s->dst_address))
	;
      else if (unformat (input, "count %f", &s->n_packets_to_send))
	;
      else if (unformat (input, "rate %f", &s->send_rate_bits_per_second))
	;
      else if (unformat (input, "size %d", &s->n_bytes_payload))
	;
      else if (unformat (input, "histogram-time %f", &s->rtt_histogram_bins_per_sec))
	s->rtt_histogram_bins_per_sec = 1 / s->rtt_histogram_bins_per_sec;
      else
	return clib_error_return (0, "parse error: %U", format_unformat_error, input);
    }

  vlib_node_set_state (vm, rtt_test_tx_node.index, VLIB_NODE_STATE_POLLING);

  vec_validate (s->rtt_histogram, pow2_mask (s->log2_n_histogram));

  s->tx_times[0] = vlib_time_now (vm);
  s->tx_times[1] = 0;
  s->n_bytes_per_packet_on_wire
    = (s->n_bytes_payload
       + sizeof (rtt_test_header_t)
       + sizeof (ip4_header_t)
       + tm->n_encap_bytes);

  s->send_rate_packets_per_second = s->send_rate_bits_per_second / (s->n_bytes_per_packet_on_wire * BITS (u8));
  clib_warning ("%d bytes on wire %.4epps",
		s->n_bytes_per_packet_on_wire,
		s->send_rate_packets_per_second);

  {
    rtt_test_packet_t * t;
    int i;

    t = clib_mem_alloc_no_fail (sizeof (t[0]) + s->n_bytes_payload);
    memset (t, 0, sizeof (t[0]));

    t->ip4.ip_version_and_header_length = 0x45;
    t->ip4.length = clib_host_to_net_u16 (sizeof (t[0]) + s->n_bytes_payload);
    t->ip4.flags_and_fragment_offset = clib_host_to_net_u16 (IP4_HEADER_FLAG_DONT_FRAGMENT);
    t->ip4.protocol = tm->my_ip_protocol;
    t->ip4.ttl = 64;

    t->ip4.src_address = s->src_address;
    t->ip4.dst_address = s->dst_address;
    
    t->ip4.checksum = ip4_header_checksum (&t->ip4);

    t->rtt.stream_index = s - tm->stream_pool;

    for (i = 0; i < s->n_bytes_payload; i++)
      t->payload[i] = i;

    vlib_packet_template_init (vm, &s->packet_template,
			       t, sizeof (t[0]) + s->n_bytes_payload,
			       /* alloc chunk size */ VLIB_FRAME_SIZE,
			       VNET_BUFFER_LOCALLY_GENERATED,
			       "rtt-test stream %d data", s - tm->stream_pool);

    clib_mem_free (t);
  }

  return 0;
}

VLIB_CLI_COMMAND (rtt_test_cli_command) = {
  .name = "rtt",
  .short_help = "Measure RTT test protocol",
  .parent = &vlib_cli_test_command,
  .function = rtt_test_command,
};

static u8 * format_rtt_test_stream (u8 * s, va_list * args)
{
  rtt_test_stream_t * t = va_arg (*args, rtt_test_stream_t *);
  f64 ave, rms, count;
  uword indent = format_get_indent (s);

  s = format (s, "%U -> %U",
	      format_ip4_address, &t->src_address,
	      format_ip4_address, &t->dst_address);

  s = format (s, "\n%U  sent %Ld, received: from-past %Ld in-sequence %Ld from-future %Ld",
	      format_white_space, indent,
	      t->n_packets_sent,
	      t->n_rx[0], t->n_rx[1], t->n_rx[2]);

  s = format (s, "\n%U  rx-rate %.4e bits/sec",
	      format_white_space, indent,
	      (((f64) (t->n_rx[0] + t->n_rx[1] + t->n_rx[2]) * (f64) t->n_bytes_per_packet_on_wire * BITS (u8))
	       / (t->rx_ack_times[1] - t->rx_ack_times[0])));
	       
  count = t->n_rx[1];
  if (count > 0)
    {
      ave = t->sum_dt / count;
      rms = sqrt (t->sum_dt2 / count - ave*ave);
      s = format (s, "\n%U  rtt %.4e +- %.4e",
		  format_white_space, indent,
		  ave, rms);
    }

  if (0) {
    u32 i;
    s = format (s, "\n%U", format_white_space, indent);
    for (i = 0; i < vec_len (t->rtt_histogram); i++)
      {
	if (t->rtt_histogram[i] > 0)
	  s = format (s, ", %d %d", i, t->rtt_histogram[i]);
      }
  }

  return s;
}

static clib_error_t *
rtt_show_command (vlib_main_t * vm,
		  unformat_input_t * input,
		  vlib_cli_command_t * cmd)
{
  rtt_test_main_t * tm = &rtt_test_main;
  rtt_test_stream_t * s;
  int i;

  for (i = 0; i < ARRAY_LEN (tm->stream_history); i++)
    {
      s = tm->stream_history + i;
      if (s->n_packets_sent > 0)
	vlib_cli_output (vm, "%U", format_rtt_test_stream, s);
    }

  pool_foreach (s, tm->stream_pool, ({
    vlib_cli_output (vm, "%U", format_rtt_test_stream, s);
  }));

  return 0;
}

VLIB_CLI_COMMAND (rtt_show_cli_command) = {
  .name = "rtt",
  .short_help = "Show RTT measurements",
  .parent = &vlib_cli_show_command,
  .function = rtt_show_command,
};

static clib_error_t *
rtt_test_init (vlib_main_t * vm)
{
  rtt_test_main_t * tm = &rtt_test_main;

  tm->my_ip_protocol = IP_PROTOCOL_CHAOS;

  ip4_register_protocol (tm->my_ip_protocol, rtt_test_rx_node.index);

  {
    rtt_test_packet_t ack;

    memset (&ack, 0, sizeof (ack));

    ack.ip4.ip_version_and_header_length = 0x45;
    ack.ip4.length = clib_host_to_net_u16 (sizeof (ack));
    ack.ip4.flags_and_fragment_offset = clib_host_to_net_u16 (IP4_HEADER_FLAG_DONT_FRAGMENT);
    ack.ip4.protocol = tm->my_ip_protocol;
    ack.ip4.ttl = 64;

    ack.ip4.checksum = ip4_header_checksum (&ack.ip4);
    tm->ack_packet_template_ip4_checksum = ack.ip4.checksum;

    vlib_packet_template_init (vm, &tm->ack_packet_template,
			       &ack,
			       sizeof (ack),
			       /* alloc chunk size */ VLIB_FRAME_SIZE,
			       VNET_BUFFER_LOCALLY_GENERATED,
			       "rtt-test ack");
  }

  return /* no error */ 0;
}

static VLIB_INIT_FUNCTION (rtt_test_init);

static clib_error_t *
rtt_test_config (vlib_main_t * vm, unformat_input_t * input)
{
  rtt_test_main_t * tm = &rtt_test_main;
  clib_error_t * error = 0;

  tm->print_status_every_n_packets_sent = 0;
  tm->n_encap_bytes = 14 + 12 + 8;	/* size of ethernet header */
  tm->verbose = 1;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "print %f", &tm->print_status_every_n_packets_sent))
	;
      else if (unformat (input, "silent"))
	tm->verbose = 0;
      else
	clib_error ("%U", format_unformat_error, input);
    }

  return error;
}

VLIB_CONFIG_FUNCTION (rtt_test_config, "rtt-test");
