/*
 * docsis/cm_input.c: control/management input for cable modems
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
cm_input_time_synchronization (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  u32 * cmts_time_stamp = (void *) d->payload;
  ASSERT (d == 0);
  ASSERT (*cmts_time_stamp == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_channel_descriptor (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_bandwidth_allocation_map (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_ranging_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_registration_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_channel_change_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_privacy_key_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_registration_ack (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_service_add_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_service_add_ack (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_service_change_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_service_change_ack (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_service_del_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_channel_change_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_channel_change_ack (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_device_class_id_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_tx_disable (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_channel_descriptor_docsis_2_or_3 (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_downstream_channel_descriptor (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_mac_domain_descriptor (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_upstream_channel_descriptor_docsis_3 (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_bonding_change_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_dynamic_bonding_change_ack (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_path_verify_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_cable_modem_control_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static docsis_node_error_t cm_input_multipart_registration_response (docsis_main_t * dm, vlib_buffer_t * b)
{
  docsis_management_packet_t * d = vlib_buffer_get_current (b);
  ASSERT (d == 0);
  return DOCSIS_ERROR_NONE;
}

static clib_error_t * docsis_input_cm_init (vlib_main_t * vm)
{
  docsis_main_t * dm = &docsis_main;

  /* Management packet types we want. */
#define _(f) \
  dm->input_functions_for_role[DOCSIS_ROLE_CM].management[DOCSIS_MANAGEMENT_PACKET_TYPE_##f] = cm_input_##f;

  _ (time_synchronization);
  _ (upstream_channel_descriptor);
  _ (upstream_bandwidth_allocation_map);
  _ (ranging_response);
  _ (registration_response);
  _ (upstream_channel_change_response);
  _ (privacy_key_response);
  _ (registration_ack);
  _ (dynamic_service_add_response);
  _ (dynamic_service_add_ack);
  _ (dynamic_service_change_response);
  _ (dynamic_service_change_ack);
  _ (dynamic_service_del_response);
  _ (dynamic_channel_change_response);
  _ (dynamic_channel_change_ack);
  _ (device_class_id_response);
  _ (upstream_tx_disable);
  _ (upstream_channel_descriptor_docsis_2_or_3);
  _ (downstream_channel_descriptor);
  _ (mac_domain_descriptor);
  _ (upstream_channel_descriptor_docsis_3);
  _ (dynamic_bonding_change_response);
  _ (dynamic_bonding_change_ack);
  _ (path_verify_response);
  _ (cable_modem_control_response);
  _ (multipart_registration_response);

#undef _

  return 0;
}

VLIB_INIT_FUNCTION (docsis_input_cm_init);
