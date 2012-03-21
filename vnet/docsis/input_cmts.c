/*
 * docsis/cmts_input.c: control/management input for cable modem termination
 *
 * Copyright (c) 2012 Eliot Dresselhaus
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
#include <vnet/docsis/docsis.h>

static docsis_node_error_t
cmts_input_request_frame (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_CONTROL_FRAME_HANDLED;
}

static docsis_node_error_t
cmts_input_fragmentation (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_CONTROL_FRAME_HANDLED;
}

static docsis_node_error_t
cmts_input_queue_depth_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_CONTROL_FRAME_HANDLED;
}

static docsis_node_error_t
cmts_input_concatenation (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_CONTROL_FRAME_HANDLED;
}

static docsis_node_error_t cmts_input_ranging_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_registration_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_upstream_channel_change_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_privacy_key_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_dynamic_service_add_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_dynamic_service_change_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_dynamic_service_del_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_dynamic_channel_change_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_device_class_id_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_initial_ranging_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_test_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_bonded_initial_ranging_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_dynamic_bonding_change_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_path_verify_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_cable_modem_status_report (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_cable_modem_control_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cmts_input_multipart_registration_request (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static clib_error_t * docsis_input_cmts_init (vlib_main_t * vm)
{
  docsis_main_t * dm = &docsis_main;

  /* Control packet types we want. */
#define _(f) \
  dm->input_functions_for_role[DOCSIS_ROLE_CMTS].control[DOCSIS_CONTROL_PACKET_TYPE_##f] = cmts_input_##f;

  _ (request_frame);
  _ (fragmentation);
  _ (queue_depth_request);
  _ (concatenation);

#undef _

  /* Management packet types we want. */
#define _(f) \
  dm->input_functions_for_role[DOCSIS_ROLE_CMTS].management[DOCSIS_MANAGEMENT_PACKET_TYPE_##f] = cmts_input_##f;

  _ (ranging_request);
  _ (registration_request);
  _ (upstream_channel_change_request);
  _ (privacy_key_request);
  _ (dynamic_service_add_request);
  _ (dynamic_service_change_request);
  _ (dynamic_service_del_request);
  _ (dynamic_channel_change_request);
  _ (device_class_id_request);
  _ (initial_ranging_request);
  _ (test_request);
  _ (bonded_initial_ranging_request);
  _ (dynamic_bonding_change_request);
  _ (path_verify_request);
  _ (cable_modem_status_report);
  _ (cable_modem_control_request);
  _ (multipart_registration_request);

#undef _

  return 0;
}

VLIB_INIT_FUNCTION (docsis_input_cmts_init);
