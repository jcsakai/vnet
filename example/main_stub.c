#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include <vlib/unix/pci.h>
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ip/ip.h>
#include <vnet/ip/tcp.h>

static clib_error_t *
vnet_main_init (vlib_main_t * vm)
{
  clib_error_t * error = 0;

  if ((error = vlib_call_init_function (vm, pg_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ip_main_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ethernet_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ethernet_arp_init)))
    return error;
  if ((error = vlib_call_init_function (vm, osi_init)))
    return error;

  if ((error = vlib_call_init_function (vm, ixge_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ige_init)))
    return error;

  if ((error = unix_physmem_init (vm, /* physical_memory_required */ 0)))
    return error;

  if ((error = unix_physmem_init (vm, /* physical_memory_required */ 0)))
    return error;

  if ((error = vlib_call_init_function (vm, tuntap_init)))
    return error;

  vlib_unix_cli_set_prompt ("VNET: ");

  return error;
}

static VLIB_INIT_FUNCTION (vnet_main_init);

int main (int argc, char * argv[])
{
  clib_mem_init (0, 256 << 20);
  return vlib_unix_main (argc, argv);
}

#define foreach_tcp_test_error			\
  _ (SEGMENTS_RECEIVED, "segments received")

typedef enum {
#define _(sym,str) TCP_TEST_ERROR_##sym,
  foreach_tcp_test_error
#undef _
  TCP_TEST_N_ERROR,
} tcp_test_error_t;

static char * tcp_test_error_strings[] = {
#define _(sym,string) string,
  foreach_tcp_test_error
#undef _
};

static uword
tcp_test (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  u32 bi0;
	  u8 error0, next0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);

	  clib_warning ("got '%U'", format_vlib_buffer_contents, vm, p0);

	  error0 = next0 = 0;
	  p0->error = node->errors[error0];

	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

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

VLIB_REGISTER_NODE (tcp_test_node) = {
  .function = tcp_test,
  .name = "tcp-test",

  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },

  .n_errors = TCP_TEST_N_ERROR,
  .error_strings = tcp_test_error_strings,
};

static clib_error_t *
tcp_test_init (vlib_main_t * vm)
{
  clib_error_t * error = 0;

  {
    tcp_listener_registration_t r = {
      .port = 1234,
      .flags = TCP_LISTENER_IP4,
      .data_node_index = tcp_test_node.index,
      .event_function = 0,
    };

    tcp_register_listener (vm, &r);
  }

  return error;
}

static VLIB_INIT_FUNCTION (tcp_test_init);
