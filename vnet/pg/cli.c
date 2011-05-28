/*
 * pg_cli.c: packet generator cli
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

#ifdef CLIB_UNIX
#include <vnet/unix/pcap.h>
#endif

/* Root of all packet generator cli commands. */
static VLIB_CLI_COMMAND (vlib_cli_pg_command) = {
  .path = "packet-generator",
  .short_help = "Packet generator commands",
};

static clib_error_t *
enable_disable_stream (vlib_main_t * vm,
		       unformat_input_t * input,
		       vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;
  int is_enable = cmd->function_arg != 0;
  u32 stream_index = ~0;

  if (unformat (input, "%U", unformat_eof))
    ;
  else if (unformat (input, "%U", unformat_hash_vec_string,
		     pg->stream_index_by_name, &stream_index))
    ;
  else
    return clib_error_create ("unknown input `%U'",
			      format_unformat_error, input);

  /* No stream specified: enable/disable all streams. */
  if (stream_index == ~0)
    pool_foreach (s, pg->streams, ({
      pg_stream_enable_disable (pg, s, is_enable);
    }));
  else
    {
      /* enable/disable specified stream. */
      s = pool_elt_at_index (pg->streams, stream_index);
      pg_stream_enable_disable (pg, s, is_enable);
    }
		      
  return 0;
}

static VLIB_CLI_COMMAND (enable_streams_cli) = {
  .path = "packet-generator enable-stream",
  .short_help = "Enable packet generator streams",
  .function = enable_disable_stream,
  .function_arg = 1,		/* is_enable */
};

static VLIB_CLI_COMMAND (disable_streams_cli) = {
  .path = "packet-generator disable-stream",
  .short_help = "Disable packet generator streams",
  .function = enable_disable_stream,
  .function_arg = 0,		/* is_enable */
};

static u8 * format_pg_stream (u8 * s, va_list * va)
{
  pg_stream_t * t = va_arg (*va, pg_stream_t *);
  u8 * v;

  if (! t)
    return format (s, "%=16s%=12s%=16s%s",
		   "Name", "Enabled", "Count", "Parameters");

  s = format (s, "%-16v%=12s%16Ld",
	      t->name,
	      pg_stream_is_enabled (t) ? "Yes" : "No",
	      t->n_packets_generated);

  v = 0;

  v = format (v, "limit %Ld, ", t->n_packets_limit);

  v = format (v, "rate %.2e pps, ", t->rate_packets_per_second);

  v = format (v, "size %d%c%d, ",
	      t->min_packet_bytes,
	      t->packet_size_edit_type == PG_EDIT_RANDOM ? '+' : '-',
	      t->max_packet_bytes);

  v = format (v, "buffer-size %d, ", t->buffer_bytes);

  if (v)
    {
      s = format (s, "  %v", v);
      vec_free (v);
    }

  return s;
}

static clib_error_t *
show_streams (vlib_main_t * vm,
	      unformat_input_t * input,
	      vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;

  if (pool_elts (pg->streams) == 0)
    {
      vlib_cli_output (vm, "no streams currently defined");
      goto done;
    }

  vlib_cli_output (vm, "%U", format_pg_stream, 0);
  pool_foreach (s, pg->streams, ({
      vlib_cli_output (vm, "%U", format_pg_stream, s);
    }));

 done:
  return 0;
}

static VLIB_CLI_COMMAND (show_streams_cli) = {
  .path = "show packet-generator",
  .short_help = "Show packet generator streams",
  .function = show_streams,
};

static clib_error_t *
pg_pcap_read (pg_stream_t * s, char * file_name)
{
#ifndef CLIB_UNIX
  return clib_error_return (0, "no pcap support");
#else
  pcap_main_t pm;
  clib_error_t * error;
  memset (&pm, 0, sizeof (pm));
  pm.file_name = file_name;
  error = pcap_read (&pm);
  s->replay_packet_templates = pm.packets_read;
  s->min_packet_bytes = pm.min_packet_bytes;
  s->max_packet_bytes = pm.max_packet_bytes;
  s->buffer_bytes = pm.max_packet_bytes;
  /* For PCAP buffers we never re-use buffers. */
  s->flags |= PG_STREAM_FLAGS_DISABLE_BUFFER_RECYCLE;
  return error;
#endif /* CLIB_UNIX */
}

static uword
unformat_pg_stream_parameter (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  f64 x;

  if (unformat (input, "limit %f", &x))
    s->n_packets_limit = x;

  else if (unformat (input, "rate %f", &x))
    s->rate_packets_per_second = x;

  else if (unformat (input, "size %d-%d", &s->min_packet_bytes,
		     &s->max_packet_bytes))
    s->packet_size_edit_type = PG_EDIT_INCREMENT;

  else if (unformat (input, "size %d+%d", &s->min_packet_bytes,
		     &s->max_packet_bytes))
    s->packet_size_edit_type = PG_EDIT_RANDOM;

  else if (unformat (input, "buffer-size %d", &s->buffer_bytes))
    ;

  else
    return 0;

  return 1;
}

static clib_error_t *
validate_stream (pg_stream_t * s)
{
  if (s->max_packet_bytes < s->min_packet_bytes)
    return clib_error_create ("max-size < min-size");

  if (s->buffer_bytes >= 4096 || s->buffer_bytes == 0)
    return clib_error_create ("buffer-size must be positive and < 4096, given %d",
			      s->buffer_bytes);

  if (s->rate_packets_per_second < 0)
    return clib_error_create ("negative rate");

  return 0;
}

static clib_error_t *
new_stream (vlib_main_t * vm,
	    unformat_input_t * input,
	    vlib_cli_command_t * cmd)
{
  clib_error_t * error = 0;
  u8 * tmp = 0;
  u32 hw_if_index;
  unformat_input_t sub_input = {0};
  int sub_input_given = 0;
  pg_main_t * pg = &pg_main;
  pg_stream_t s = {0};
  char * pcap_file_name;
  
  s.sw_if_index[VLIB_RX] = s.sw_if_index[VLIB_TX] = ~0;
  s.node_index = ~0;
  s.max_packet_bytes = s.min_packet_bytes = 64;
  s.buffer_bytes = VLIB_BUFFER_DEFAULT_FREE_LIST_BYTES;
  pcap_file_name = 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "name %v", &tmp))
	{
	  if (s.name)
	    vec_free (s.name);
	  s.name = tmp;
	}

      else if (unformat (input, "node %U",
			 unformat_vlib_hw_interface, vm, &hw_if_index))
	{
	  vlib_hw_interface_t * hi = vlib_get_hw_interface (vm, hw_if_index);

	  s.node_index = hi->output_node_index;
	  s.sw_if_index[VLIB_TX] = hi->sw_if_index;
	}

      else if (unformat (input, "node %U",
			 unformat_vlib_node, vm, &s.node_index))
	;
			 
      else if (unformat (input, "interface %U",
			 unformat_vlib_sw_interface, vm, &s.sw_if_index[VLIB_RX]))
	;

      else if (unformat (input, "pcap %s", &pcap_file_name))
	;

      else if (! sub_input_given
	       && unformat (input, "data %U", unformat_input, &sub_input))
	sub_input_given++;
			 
      else if (unformat_user (input, unformat_pg_stream_parameter, &s))
	;

      else if (unformat (input, "no-recycle"))
	s.flags |= PG_STREAM_FLAGS_DISABLE_BUFFER_RECYCLE;

      else
	{
	  error = clib_error_create ("unknown input `%U'",
				     format_unformat_error, input);
	  goto done;
	}
    }

  error = validate_stream (&s);
  if (error)
    return error;

  if (! sub_input_given && ! pcap_file_name)
    {
      error = clib_error_create ("no packet data given");
      goto done;
    }

  if (s.node_index == ~0)
    {
      error = clib_error_create ("output interface or node not given");
      goto done;
    }

  {
    pg_node_t * n;

    if (s.node_index < vec_len (pg->nodes))
      n = pg->nodes + s.node_index;
    else
      n = 0;

    if (pcap_file_name != 0)
      {
	error = pg_pcap_read (&s, pcap_file_name);
	if (error)
	  goto done;
	vec_free (pcap_file_name);
      }

    else if (n && n->unformat_edit
	&& unformat_user (&sub_input, n->unformat_edit, &s))
      ;

    else if (! unformat_user (&sub_input, unformat_pg_payload, &s))
      {
	error = clib_error_create
	  ("failed to parse packet data from `%U'",
	   format_unformat_error, &sub_input);
	goto done;
      }
  }

  pg_stream_add (pg, &s);
  return 0;

 done:
  pg_stream_free (&s);
  unformat_free (&sub_input);
  return error;
}

static VLIB_CLI_COMMAND (new_stream_cli) = {
  .path = "packet-generator new",
  .function = new_stream,
  .short_help = "Create packet generator stream",
  .long_help =
  "Create packet generator stream\n"
  "\n"
  "Arguments:\n"
  "\n"
  "name STRING          sets stream name\n"
  "interface STRING     interface for stream output \n"
  "node NODE-NAME       node for stream output\n"
  "data STRING          specifies packet data\n",
};

static clib_error_t *
del_stream (vlib_main_t * vm,
	    unformat_input_t * input,
	    vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  u32 i;
  
  if (! unformat (input, "%U",
		  &unformat_hash_vec_string, pg->stream_index_by_name, &i))
    return clib_error_create ("expected stream name `%U'",
			      format_unformat_error, input);

  pg_stream_del (pg, i);
  return 0;
}

static VLIB_CLI_COMMAND (del_stream_cli) = {
  .path = "packet-generator delete",
  .function = del_stream,
  .short_help = "Delete stream with given name",
};

static clib_error_t *
change_stream_parameters (vlib_main_t * vm,
			  unformat_input_t * input,
			  vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s, s_new;
  u32 stream_index = ~0;
  clib_error_t * error;

  if (unformat (input, "%U", unformat_hash_vec_string,
		pg->stream_index_by_name, &stream_index))
    ;
  else
    return clib_error_create ("expecting stream name; got `%U'",
			      format_unformat_error, input);

  s = pool_elt_at_index (pg->streams, stream_index);
  s_new = s[0];

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat_user (input, unformat_pg_stream_parameter, &s_new))
	;

      else
	return clib_error_create ("unknown input `%U'",
				  format_unformat_error, input);
    }

  error = validate_stream (&s_new);
  if (! error)
    s[0] = s_new;

  return error;
}

static VLIB_CLI_COMMAND (change_stream_parameters_cli) = {
  .path = "packet-generator configure",
  .short_help = "Change packet generator stream parameters",
  .function = change_stream_parameters,
};

/* Dummy init function so that we can be linked in. */
static clib_error_t * pg_cli_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (pg_cli_init);
