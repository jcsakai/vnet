#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

int main (int argc, char * argv[])
{
  return vlib_unix_main (argc, argv);
}


#if 0
/* Node test code. */
typedef struct {
  int scalar;
  int vector[0];
} my_frame_t;

static u8 * format_my_node_frame (u8 * s, va_list * va)
{
  vlib_frame_t * f = va_arg (*va, vlib_frame_t *);
  my_frame_t * g = vlib_frame_args (f);
  int i;

  s = format (s, "scalar %d, vector { ", g->scalar);
  for (i = 0; i < f->n_vectors; i++)
    s = format (s, "%d, ", g->vector[i]);
  s = format (s, " }");

  return s;
}

static uword
my_func (vlib_main_t * vm,
	 vlib_node_runtime_t * rt,
	 vlib_frame_t * f)
{
  vlib_node_t * node;
  my_frame_t * y;
  u32 i, n_left = 0;
  static int serial;
  int verbose;

  node = vlib_get_node (vm, rt->node_index);

  verbose = 0;

  if (verbose && f)
    vlib_cli_output (vm, "%v: call frame %p %U", node->name,
		     f, format_my_node_frame, f);

  if (rt->n_next_nodes > 0)
    {
      vlib_frame_t * next = vlib_get_next_frame (vm, rt, /* next index */ 0);
      n_left = VLIB_FRAME_SIZE - next->n_vectors;
      y = vlib_frame_args (next);
      y->scalar = serial++;
    }
  else
    y = 0;

  for (i = 0; i < 5; i++)
    {
      if (y)
	{
	  ASSERT (n_left > 0);
	  n_left--;
	  y->vector[i] = y->scalar + i;
	}
    }
  if (y)
    vlib_put_next_frame (vm, rt, /* next index */ 0, n_left);

  if (verbose)
    vlib_cli_output (vm, "%v: return frame %p", node->name, f);

  return i;
}

static VLIB_REGISTER_NODE (my_node1) = {
  .function = my_func,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "my-node1",
  .scalar_size = sizeof (my_frame_t),
  .vector_size = STRUCT_SIZE_OF (my_frame_t, vector[0]),
  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "my-node2",
  },
};

static VLIB_REGISTER_NODE (my_node2) = {
  .function = my_func,
  .name = "my-node2",
  .scalar_size = sizeof (my_frame_t),
  .vector_size = STRUCT_SIZE_OF (my_frame_t, vector[0]),
};

#endif

#if 1

typedef enum {
  MY_EVENT_TYPE1,
  MY_EVENT_TYPE2,
} my_process_completion_type_t;

static uword
my_proc (vlib_main_t * vm,
	 vlib_node_runtime_t * rt,
	 vlib_frame_t * f)
{
  vlib_node_t * node;
  u32 i;

  node = vlib_get_node (vm, rt->node_index);

  vlib_cli_output (vm, "%v: call frame %p", node->name, f);

  for (i = 0; i < 5; i++)
    {
      vlib_cli_output (vm, "%v: %d", node->name, i);
      vlib_process_suspend (vm, 1e0 /* secs */);
    }

  vlib_cli_output (vm, "%v: return frame %p", node->name, f);

  {
    uword n_events_seen, type, * data = 0;

    for (n_events_seen = 0; n_events_seen < 2;)
      {
	vlib_process_wait_for_event (vm);
	type = vlib_process_get_events (vm, &data);
	n_events_seen += vec_len (data);
	vlib_cli_output (vm, "%v: completion #%d type %d data 0x%wx",
			 node->name, i, type, data[0]);
	_vec_len (data) = 0;
      }

    vec_free (data);
  }

  vlib_node_enable_disable (vm, rt->node_index, /* enable */ 0);

  return i;
}

static VLIB_REGISTER_NODE (my_proc_node) = {
  .function = my_proc,
  .type = VLIB_NODE_TYPE_PROCESS,
  .name = "my-proc",
};

static uword
my_proc_input (vlib_main_t * vm,
	       vlib_node_runtime_t * rt,
	       vlib_frame_t * f)
{
  static int i;

  if (i++ < 2)
    vlib_process_signal_event (vm, my_proc_node.index,
			       i == 1 ? MY_EVENT_TYPE1 : MY_EVENT_TYPE2,
			       0x12340000 + i);
  else
    vlib_node_enable_disable (vm, rt->node_index, /* enable */ 0);

  return 0; 
}

static VLIB_REGISTER_NODE (my_proc_input_node) = {
  .function = my_proc_input,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "my-proc-input",
};
#endif
