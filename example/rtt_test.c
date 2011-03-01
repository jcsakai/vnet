#include <vnet/ip/ip.h>
#include <math.h>

/* 20 byte TCP + 12 bytes of options (timestamps) = 32 bytes */
typedef struct {
  u64 sequence_number;
  f64 time_stamp;
  u32 stream_index;
  u32 is_ack;
  u32 unused[2];
  u8 payload[0];
} rtt_test_header_t;

typedef struct {
  ip4_address_t peer_ip4_address;

  f64 n_packets_to_send;

  f64 send_rate_bits_per_second;

  u32 max_payload_size;

  u64 n_tx;
  u64 n_rx[3];

  u64 rx_expected_sequence_number;

  f64 sum_dt, sum_dt2;

  u32 * rtt_histogram;
} rtt_test_stream_t;

typedef struct {
  /* Size of encapsulation (e.g. 14 for ethernet). */
  u32 n_encap_bytes;

  u32 is_sender;

  u32 verbose;

  f64 print_status_every_n_packets_sent;

  vlib_packet_template_t packet_template;

  f64 rtt_histogram_bins_per_sec;

  rtt_test_stream_t * stream_pool;
} rtt_test_main_t;

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

always_inline void
vlib_buffer_truncate_length (vlib_main_t * vm, vlib_buffer_t * b, u32 new_length)
{
  ASSERT (new_length <= b->current_length);
  b->current_length = new_length;
  while (b->flags & VLIB_BUFFER_NEXT_PRESENT)
    {
      b = vlib_get_buffer (vm, b->next_buffer);
      b->current_length = 0;
    }
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
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  u8 host_config_ttl = ip4_main.host_config.ttl;
  u16 echo_length_host = sizeof (ip4_header_t) + sizeof (rtt_test_header_t);
  u16 echo_length_net = clib_host_to_net_u16 (echo_length_host);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = RTT_TEST_RX_NEXT_ECHO;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  rtt_test_header_t * r0;
	  ip_csum_t sum0;
	  u32 bi0, src0, dst0, next0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);

	  src0 = ip0->src_address.data_u32;
	  dst0 = ip0->dst_address.data_u32;
	  ip0->src_address.data_u32 = dst0;
	  ip0->dst_address.data_u32 = src0;

	  sum0 = ip0->checksum;

	  sum0 = ip_csum_update (sum0, ip0->ttl, host_config_ttl,
				 ip4_header_t, ttl);
	  ip0->ttl = host_config_ttl;

	  sum0 = ip_csum_update (sum0, ip0->length, echo_length_net,
				 ip4_header_t, length);

	  ip0->length = echo_length_net;

	  ip0->checksum = ip_csum_fold (sum0);

	  ASSERT (ip0->checksum == ip4_header_checksum (ip0));

	  vlib_buffer_truncate_length (vm, p0, echo_length_host);

	  r0 = ip4_next_header (ip0);

	  /* Don't ack acks. */
	  next0 = r0->is_ack ? RTT_TEST_RX_NEXT_DROP : RTT_TEST_RX_NEXT_ECHO;
	  p0->error = node->errors[RTT_TEST_ERROR_listener_acks_dropped];

	  r0->is_ack = 1;
	  if (PREDICT_FALSE (next0 != next))
	    {
	      vlib_put_next_frame (vm, node, next, n_left_to_next + 1);
	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
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
  static rtt_test_stream_t dummy_stream;

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
	  rtt_test_stream_t * s0;
	  u32 bi0, bin0, i0, error0;
	  i64 seq_cmp0;
	  f64 dt0;
      
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

	  s0 = (error0 == RTT_TEST_ERROR_unknown_stream
		? &dummy_stream
		: pool_elt_at_index (tm->stream_pool, r0->stream_index));

	  p0->error = node->errors[error0];

	  dt0 = now - r0->time_stamp;
	  seq_cmp0 = (i64) r0->sequence_number - (i64) s0->rx_expected_sequence_number;

	  i0 = seq_cmp0 < 0 ? 0 : (seq_cmp0 == 0 ? 1 : 2);
	  s0->rx_expected_sequence_number = r0->sequence_number + (i0 > 0);
	  s0->n_rx[i0] += 1;

	  s0->sum_dt += dt0;
	  s0->sum_dt2 += dt0*dt0;

	  bin0 = flt_round_nearest (dt0 * tm->rtt_histogram_bins_per_sec);
	  vec_validate (s0->rtt_histogram, bin0);
	  s0->rtt_histogram[bin0] += 1;
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
    [RTT_TEST_RX_NEXT_ECHO] = "ip4-lookup",
  },

  .n_errors = RTT_TEST_N_ERROR,
  .error_strings = rtt_test_error_strings,
};

static clib_error_t *
rtt_test_init (vlib_main_t * vm)
{
  clib_error_t * error = 0;

  ip4_register_protocol (IP_PROTOCOL_CHAOS, rtt_test_rx_node.index);

  return error;
}

static VLIB_INIT_FUNCTION (rtt_test_init);

static clib_error_t *
rtt_test_config (vlib_main_t * vm, unformat_input_t * input)
{
  rtt_test_main_t * tm = &rtt_test_main;
  clib_error_t * error = 0;

  tm->print_status_every_n_packets_sent = 0;
  tm->n_encap_bytes = 14;	/* size of ethernet header */
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
