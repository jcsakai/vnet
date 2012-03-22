#ifndef included_docsis_packet_h
#define included_docsis_packet_h

#include <clib/clib.h>
#include <vnet/ethernet/packet.h> /* for ethernet_header_t */
#include <vnet/llc/llc.h>	  /* for llc_header_t */

/* FC_TYPE in DOCSIS spec. */
#define foreach_docsis_packet_type			\
  _ (ethernet)						\
  _ (atm)						\
  _ (isolation_ethernet)				\
  _ (control)

/* Type of control frame is encoded in 5 bit mac_parm field.
   _ (symbol, number). */
#define foreach_docsis_control_packet_type	\
  _ (timing_management, 0x0)			\
  _ (management, 0x1)				\
  _ (request_frame, 0x2)			\
  _ (fragmentation, 0x3)			\
  _ (queue_depth_request, 0x4)			\
  _ (concatenation, 0x1c)
  
typedef enum {
#define _(f) DOCSIS_PACKET_TYPE_##f,
  foreach_docsis_packet_type
#undef _
} docsis_packet_type_t;

typedef enum {
#define _(f,n) DOCSIS_CONTROL_PACKET_TYPE_##f = n,
  foreach_docsis_control_packet_type
#undef _
} docsis_control_packet_type_t;

/* All packets start with 8 bit header. */
typedef union {
  CLIB_PACKED (struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
    docsis_packet_type_t packet_type : 2;

    /* Extended packet type for packet_type == control. */
    docsis_control_packet_type_t control_packet_type : 5;

    u8 extended_header_present : 1;
#else
    u8 extended_header_present : 1;

    /* Extended packet type for packet_type == control. */
    docsis_control_packet_type_t control_packet_type : 5;

    docsis_packet_type_t packet_type : 2;
#endif
  });

  u8 as_u8;
} docsis_packet_header_t;

always_inline uword
docsis_packet_header_is_management (docsis_packet_header_t h)
{ return (h.packet_type == DOCSIS_PACKET_TYPE_control
	  && (h.control_packet_type == DOCSIS_CONTROL_PACKET_TYPE_management
	      || h.control_packet_type == DOCSIS_CONTROL_PACKET_TYPE_timing_management)); }

always_inline uword
docsis_packet_header_is_ethernet_data_packet (docsis_packet_header_t h)
{ return (h.packet_type == DOCSIS_PACKET_TYPE_ethernet
	  || h.packet_type == DOCSIS_PACKET_TYPE_isolation_ethernet); }

typedef u16 docsis_service_id_t;

/* Service IDs.
   0 => no CM
   0x1ffe
     special encoding for mpeg2 data
   Multicast special encodings:
   0x3eMM
     bits set P in 8 bit mask MM enable tx of priority P traffic.
   0x3ff1-0x3ffe => to all CMs in bandwidth_request_or_data;
     specifies number of mini-slots (sid - 0x3ff0) => number of mini-slots of tx opportunity;
     opportunity begins on even mini-slot boundary.
   0x3fff => all CMs.
 */

typedef union {
  /* For request_frame control packets. */
  struct {
    docsis_packet_header_t header;

    u8 n_mini_slots_requested;

    u16 service_id;

    u16 expected_header_crc;
  } request_frame;

  /* For concatenation control packets. */
  struct {
    docsis_packet_header_t header;

    u8 n_frames_to_concatenate;

    /* Total number of byte of all frames to concatenate. */
    u16 n_bytes_in_payload;

    u16 expected_header_crc;
  } concatenate;

   /* For queue_depth_request control packets. */
  struct {
    docsis_packet_header_t header;

    /* Number of bytes per unit is configured separately for each service id. */
    u16 n_units_requested;

    /* Service id requesting bandwidth. */
    u16 service_id;

    u16 expected_header_crc;
  } queue_depth_request;

  /* Generic packet: everything else. */
  struct {
    docsis_packet_header_t header;

    /* Number of bytes of extended header if present. */
    u8 n_bytes_in_extended_header;

    /* Payload length + extended header length. */
    u16 n_bytes_in_payload_plus_extended_header;

    /* Extended header follows. */
    u8 extended_header[0];

    u16 expected_header_crc;

    u8 payload[0];
  } generic;

  /* So that CRC can access packet as byte string. */
  u8 as_u8[0];
} docsis_packet_t;

always_inline void *
docsis_packet_get_payload (docsis_packet_t * d)
{
  return d->generic.payload + (d->generic.header.extended_header_present ? d->generic.n_bytes_in_extended_header : 0);
}

#define foreach_docsis_extended_header_tlv_type				\
  _ (nop, 0, 0)								\
  _ (slot_request, 1, 3) /* 1 byte # mini-slots, 2 byte service-id */	\
  _ (ack_request, 2, 2)		/* service-id */			\
  _ (upstream_privacy, 3, 0)						\
  _ (downstream_privacy, 4, 4)						\
  _ (downstream_service_flow, 5, 1)					\
  _ (upstream_service_flow, 6, 0)					\
  _ (upstream_privacy2, 7, 3)						\
  _ (downstream_service, 8, 0)						\
  _ (downstream_path_verify, 9, 5)					\
  _ (extension, 15, 0)

typedef enum {
#define _(f,n,l) DOCSIS_EXTENDED_HEADER_TLV_TYPE_##f = (n),
  foreach_docsis_extended_header_tlv_type
#undef _
} docsis_extended_header_tlv_type_t;

typedef struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
  docsis_extended_header_tlv_type_t type : 4;

  /* Number of value bytes that follow. */
  u8 n_value_bytes : 4;
#else
  u8 n_value_bytes : 4;
  docsis_extended_header_tlv_type_t type : 4;
#endif

  u8 value[0];
} docsis_extended_header_tlv_t;

/* Segment headers used in multiple TX channel mode. */
typedef struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
  u16 pointer_is_valid : 1;
  u16 reserved : 1;
  /* Pointer locates start of mac header within segment. */
  u16 pointer : 14;

  u16 segment_sequence_number : 13;
  u16 request_service_id_cluster : 3;
#else
  u16 pointer : 14;
  u16 reserved : 1;
  u16 pointer_is_valid : 1;

  u16 request_service_id_cluster : 3;
  u16 segment_sequence_number : 13;
#endif

  /* Piggyback bandwidth request. */
  u16 n_units_requested;

  /* Checksum of segment header. */
  u16 expected_header_crc;
} docsis_segment_header_t;

#define foreach_docsis_management_packet_type		\
  _ (time_synchronization, 1, 1)			\
  _ (upstream_channel_descriptor, 2, 1)			\
  _ (upstream_bandwidth_allocation_map, 3, 1)		\
  _ (ranging_request, 4, 1)				\
  _ (ranging_response, 5, 1)				\
  _ (registration_request, 6, 1)			\
  _ (registration_response, 7, 1)			\
  _ (upstream_channel_change_request, 8, 1)		\
  _ (upstream_channel_change_response, 9, 1)		\
  _ (privacy_key_request, 12, 1)			\
  _ (privacy_key_response, 13, 1)			\
  _ (registration_ack, 14, 2)				\
  _ (dynamic_service_add_request, 15, 2)		\
  _ (dynamic_service_add_response, 16, 2)		\
  _ (dynamic_service_add_ack, 17, 2)			\
  _ (dynamic_service_change_request, 18, 2)		\
  _ (dynamic_service_change_response, 19, 2)		\
  _ (dynamic_service_change_ack, 20, 2)			\
  _ (dynamic_service_del_request, 21, 2)		\
  _ (dynamic_service_del_response, 22, 2)		\
  _ (dynamic_channel_change_request, 23, 2)		\
  _ (dynamic_channel_change_response, 24, 2)		\
  _ (dynamic_channel_change_ack, 25, 2)			\
  _ (device_class_id_request, 26, 2)			\
  _ (device_class_id_response, 27, 2)			\
  _ (upstream_tx_disable, 28, 2)			\
  _ (upstream_channel_descriptor_docsis_2_or_3, 29, 3)	\
  _ (initial_ranging_request, 30, 3)			\
  _ (test_request, 31, 3)				\
  _ (downstream_channel_descriptor, 32, 3)		\
  _ (mac_domain_descriptor, 33, 3)			\
  _ (bonded_initial_ranging_request, 34, 3)		\
  _ (upstream_channel_descriptor_docsis_3, 35, 4)	\
  _ (dynamic_bonding_change_request, 36, 4)		\
  _ (dynamic_bonding_change_response, 37, 4)		\
  _ (dynamic_bonding_change_ack, 38, 4)			\
  _ (path_verify_request, 39, 4)			\
  _ (path_verify_response, 40, 4)			\
  _ (cable_modem_status_report, 41, 4)			\
  _ (cable_modem_control_request, 42, 4)		\
  _ (cable_modem_control_response, 43, 4)		\
  _ (multipart_registration_request, 44, 4)		\
  _ (multipart_registration_response, 45, 4)

typedef enum {
#define _(f,n,v) DOCSIS_MANAGEMENT_PACKET_TYPE_##f = (n),
  foreach_docsis_management_packet_type
#undef _
} docsis_management_packet_type_t;

typedef CLIB_PACKED (struct {
  ethernet_header_t ethernet;

  llc_header_t llc;

  /* Packet type and docsis version. */
  u8 docsis_version;

  union {
    docsis_management_packet_type_t type : 8;

    u8 type_as_u8;
  };

  u8 payload[0];
}) docsis_management_packet_t;

typedef struct {
  u8 type;
  u8 n_data_bytes;
  u8 data[0];
} docsis_tlv_t;

always_inline docsis_tlv_t *
docsis_tlv_next (docsis_tlv_t * t)
{
  return (docsis_tlv_t *) (t->data + t->n_data_bytes);
}

/* UCD: upstream channel descriptors. */
typedef struct {
  u8 upstream_channel_id;

  u8 configuration_change_count;

  /* Mini slot time is 6.25usec x mini_slot_size.
     Power of 2 between 1 and 128. */
  u8 mini_slot_size;

  u8 downstream_channel_id;

  /* TLVs follow. */
  docsis_tlv_t tlvs[0];
} docsis_upstream_channel_descriptor_t;

#define foreach_docsis_upstream_channel_descriptor_tlv	\
  _ (invalid, 0)					\
  /* symbol rate; multiples of 160 kHz. */		\
  _ (symbol_rate, 1)					\
  /* upstream center frequency (Hz) */			\
  _ (frequency, 4)					\
  _ (burst_preamble_pattern, 0)				\
  _ (burst_descriptor, 0)				\
  _ (burst_descriptor_docsis_2_3, 0)			\
  _ (extended_burst_preamble_pattern, 0)		\
  /* 1 => s-cdma, 2 => tdma */				\
  _ (scdma_mode, 1)					\
  _ (scdma_spreading_intervals_per_frame, 1)		\
  _ (scdma_codes_per_mini_slot, 1)			\
  _ (scdma_number_of_active_codes, 1)			\
  _ (scdma_code_hopping_seed, 2)			\
  _ (scdma_symbol_clock_ratio_numerator, 2)		\
  _ (scdma_symbol_clock_ratio_denominator, 2)		\
  /* 32 bit timestamp, 32 bit mini-slot, 8 bit frame */	\
  _ (scdma_timestamp_snapshot, 9)			\
  _ (maintain_power_spectral_density, 1)		\
  _ (ranging_required, 1)				\
  _ (scdma_max_scheduled_codes_enable, 1)		\
  _ (ranging_hold_off_priority, 4)			\
  _ (dev_class_bitmap_allowed_to_use_this_channel, 4)	\
  _ (scdma_active_code_and_hopping_mode, 1)		\
  _ (scdma_active_codes_select, 16)			\
  _ (higher_ucd_present, 1)

/* Burst descriptors. */
#define foreach_docsis_upstream_channel_modulation_type	\
  _ (invalid)						\
  _ (qpsk)						\
  _ (16_qam)						\
  _ (8_qam)						\
  _ (32_qam)						\
  _ (64_qam)						\
  _ (128_qam)

#define foreach_docsis_burst_descriptor_tlv_type	\
  _ (invalid, 0)					\
  _ (modulation_type, 1)				\
  _ (differential_encoding, 1)				\
  _ (preamble_length, 2)				\
  _ (preamble_value_offset, 2)				\
    /* Reed solomon (k, t) coding. */			\
  _ (forward_error_correction_t_value, 1)		\
  _ (forward_error_correction_k_value, 1)		\
  _ (scrambler_seed, 2)					\
  _ (max_burst_size_in_mini_slots, 1)			\
  _ (guard_time_size, 1)				\
  _ (last_code_word_length, 1)				\
  _ (scrambler_enable, 1)				\
  _ (interleave_depth, 1)				\
  _ (interleave_block_size, 2)				\
  _ (preamble_type, 1)					\
  _ (scdma_spreader_enable, 1)				\
  _ (scdma_codes_per_sub_frame, 1)			\
  _ (scdma_interleave_step_size, 1)			\
  _ (scdma_tcm_encode_enable, 1)

#define foreach_docsis_upstream_channel_usage_code	\
  _ (invalid)						\
  _ (request_region)					\
  _ (request_or_data_region)				\
  _ (initial_maintenance)				\
  _ (station_maintenance)				\
  _ (short_data_grant)					\
  _ (long_data_grant)					\
  _ (null)						\
  _ (data_ack)						\
  _ (advanced_phy_short_data_grant)			\
  _ (advanced_phy_long_data_grant)			\
  _ (advanced_phy_unsolicited_data_grant)		\
  _ (reserved12)					\
  _ (reserved13)					\
  _ (reserved14)					\
  _ (expansion)

typedef enum {
#define _(f) DOCSIS_UPSTREAM_CHANNEL_USAGE_CODE_##f,
  foreach_docsis_upstream_channel_usage_code
#undef _
} docsis_upstream_channel_usage_code_t;

typedef CLIB_PACKED (struct {
  /* 4 or 5. */
  u8 type;

  u8 n_bytes_this_descriptor;

  docsis_upstream_channel_usage_code_t upstream_channel_usage_code : 8;

  /* Burst descriptor TLVs follow. */
  docsis_tlv_t tlvs[0];
}) docsis_burst_descriptor_t;

typedef CLIB_PACKED (struct {
  u8 configuration_change_count;
  
  u8 n_fragments;

  u8 this_fragment_sequence_number;

  docsis_tlv_t tlvs[0];
}) docsis_downstream_channel_descriptor_t;

typedef struct {
  u8 configuration_change_count;
  
  u8 n_fragments;

  u8 this_fragment_sequence_number;

  u8 current_channel_downstream_channel_id;

  docsis_tlv_t tlvs[0];
} docsis_mac_domain_descriptor_t;

#define foreach_docsis_mac_domain_descriptor_tlv		\
  _ (downstream_active_channel_list, 1, 0)			\
  _ (service_group, 2, 0)					\
  _ (downstream_ambiguity_resolution_frequency_list, 3, 0)	\
  _ (downstream_channel_profile_reporting, 4, 0)		\
  _ (ip_init_parameters, 5, 0)					\
  _ (early_authentication_enable, 6, 1)				\
  _ (upstream_active_channel_list, 7, 0)			\
  _ (upstream_ambiguity_resolution_frequency_list, 8, 0)	\
  _ (upstream_extended_frequency_range_enable, 9, 1)		\
  _ (symbol_clock_is_locked_to_master_clock, 10, 1)		\
  _ (cm_status_event_control, 11, 0)				\
  _ (upstream_tx_power_encoding_enable, 12, 1)			\
  _ (dst_ethernet_address_to_downstream_id, 13, 0)		\
  _ (cm_status_event_control1, 15, 2)				\
  _ (extended_upstream_tx_power_enable, 16, 1)

#define foreach_docsis_mac_domain_descriptor_downstream_channel_tlv	\
  _ (channel_id, 1, 1)							\
  _ (frequency_in_hz, 2, 4)						\
  _ (modulation_type, 3, 1)						\
  _ (is_primary_capable, 4, 1)						\
  _ (cm_status_event_bitmap, 5, 2)					\
  _ (channel_carries_maps_and_ucds, 6, 1)

/* Specifies group ids and which downstream channels belonging to this group. */
#define foreach_docsis_mac_domain_descriptor_downstream_service_group_tlv \
  _ (group_id, 1, 1)							\
  _ (channel_ids_this_group, 2, 0)

#define foreach_docsis_mac_domain_descriptor_downstream_channel_profile_reporting_tlv \
  _ (center_frequency_spacing, 1, 1)					\
  _ (verbose_reporting, 2, 1)						\
  _ (fragmented_profiles_supported, 3, 1)

#define foreach_docsis_mac_domain_descriptor_ip_init_parameters_tlv	\
  _ (provision_mode, 1, 1)						\
  _ (pre_registration_dsid, 2, 3)

#define foreach_docsis_mac_domain_descriptor_upstream_active_channel_list_tlv \
  _ (id, 1, 1)								\
  _ (cm_status_event_bitmap, 2, 2)

#define foreach_docsis_mac_domain_descriptor_cm_status_event_control_tlv \
  _ (event_type, 1, 1)							\
  _ (max_event_holdoff_time, 2, 2)					\
  _ (max_n_reports_this_event, 3, 1)

/* Bandwidth allocation maps. */
typedef struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
  u32 service_id : 14;

  docsis_upstream_channel_usage_code_t usage_code : 4;

  /* Time change from previous in mini-slots. */
  u32 time_interval : 14;
#else
  u32 time_interval : 14;
  docsis_upstream_channel_usage_code_t usage_code : 4;
  u32 service_id : 14;
#endif
} docsis_bandwidth_allocation_map_elt_t;

typedef struct {
  u8 upstream_channel_id;
  
  /* From upstream channel descriptor. */
  u8 configuration_change_count;

  /* Number of map elts that follow. */
  u8 n_map_elts;

  u8 reserved;

  /* Start time for allocations in this map (mini-slots). */
  u32 alloc_start_time;

  /* Latest time "processed in the upstream" whatever that means. */
  u32 latest_ack_time;

  struct {
    u8 log2_start, log2_end;
  } ranging_backoff, data_backoff;

  docsis_bandwidth_allocation_map_elt_t elts[0];
} __attribute__ ((packed)) docsis_bandwidth_allocation_map_t;

typedef struct {
  /* ranging_request, initial_ranging_request. */
  u16 service_id;

  u8 downstream_channel_id;

  union {
    /* Units of 10e-3 secs. */
    u8 pending_until_complete;

    /* For initial_ranging_request. */
    u8 upstream_channel_id;
  };
} docsis_ranging_request_t;

/* bonded_initial_ranging_request */
typedef struct {
  /* [7] pre-3.0 fragmentation supported
     [6] early authentication/encryption supported */
  u8 capabilities;

  /* Zero if unknown. */
  u8 mac_domain_downstream_service_group_id;

  u8 downstream_channel_id;

  u8 upstream_channel_id;
} docsis_bonded_ranging_request_t;

typedef CLIB_PACKED (struct {
  u16 service_id;

  u8 upstream_channel_id;

  docsis_tlv_t tlvs[0];
}) docsis_ranging_response_t;

/* TX equalization data. */
typedef struct {
  /* 4 or 9 */
  u8 type;

  u8 n_bytes_this_descriptor;

  u8 main_tap_location;

  u8 n_forward_taps_per_symbol;

  u8 n_forward_taps;

  u8 reserved;

  /* Forward taps follow. */
  struct {
    /* Real/imaginary parts. */
    i16 re, im;
  } forward_taps[0];
} docsis_ranging_tx_equalization_t;

#define foreach_docsis_ranging_response_tlv_type			\
  _ (invalid, 0)							\
  _ (tx_timing_adjust, 4)		/* signed units of 6.25usec/64 */ \
  _ (tx_power_level_adjust, 1)	/* signed units of 1/4 dB */		\
  _ (tx_frequency_offset_adjust, 2) /* signed 16 bit units of Hz */	\
  _ (tx_equalization_adjust, 0)						\
  _ (ranging_status, 1)							\
  _ (new_downstream_channel_center_frequency_in_hz, 4)			\
  _ (upstream_channel_id, 1)						\
  _ (tx_timing_adjust_fraction, 1) /* units of (6.25usec/64)/256 */	\
  _ (tx_equalization_set, 0)						\
  _ (scdma_max_scheduled_codes, 1)					\
  _ (scdma_power_headroom, 1)						\
  _ (upstream_channel_tlvs, 0)						\
  _ (t4_timeout_multiplier, 1)						\
  _ (dynamic_range_window_upper_edge, 1)

#define foreach_docsis_ranging_response_upstream_channel_tlv_type	\
  _ (invalid, 0)							\
  _ (upstream_channel_id, 1)						\
  _ (temporary_service_id, 2)						\
  _ (initialization_method, 1)						\
  _ (ranging_tlvs, 0)

#define foreach_docsis_ranging_response_upstream_channel_ranging_tlv_type \
  _ (invalid, 0)							\
  _ (deprecated, 1)							\
  _ (tx_timing_adjust, 4)						\
  _ (tx_timing_adjust_fraction, 1)					\
  _ (tx_power_level_adjust, 1)						\
  _ (tx_frequency_offset_adjust, 2)					\
  _ (ranging_status, 1)

/* Registration request. */
typedef struct {
  /* Temporary service id chosen by CM. */
  u16 temporary_service_id;

  docsis_tlv_t tlvs[0];
} docsis_registration_request_t;

typedef struct {
  /* Temporary service id chosen by CM. */
  u16 temporary_service_id;

  u8 n_fragments;

  u8 this_fragment_sequence_number;

  docsis_tlv_t tlvs[0];
} docsis_multipart_registration_request_t;

#define foreach_docsis_confirmation_code	\
_ (ok, 0)					\
_ (unspecified_failure, 1)			\
_ (bad_config_setting, 2)			\
_ (out_of_resources, 3)				\
_ (admin_reject, 4)				\
_ (not_owner, 5)				\
_ (service_flow_not_found, 6)			\
_ (service_flow_already_exists, 7)		\
_ (required_parameter_not_present, 8)		\
_ (header_supression_not_supported, 9)		\
_ (transaction_id_unknown, 10)			\
_ (authentication_failure, 11)			\
_ (dynamic_service_add_aborted, 12)		\
_ (multiple_errors, 13)				\
_ (classifier_unknown, 14)			\
_ (classifier_already_exists, 15)		\
_ (header_supression_unknown_rule, 16)		\
_ (header_supression_rule_already_exists, 17)	\
_ (duplicate_reference_id, 18)			\
_ (multiple_upstream_service_flows, 19)		\
_ (multiple_downstream_service_flows, 20)
/* etc */

typedef CLIB_PACKED (struct {
  /* Temporary service id chosen by CM in request. */
  u16 temporary_service_id;

  /* 0 ok, 1 authentication failure, 2 class of service failure.
     confirmation code for ack. */
  u8 response;

  docsis_tlv_t tlvs[0];
}) docsis_registration_response_t;

typedef CLIB_PACKED (struct {
  /* Temporary service id chosen by CM in request. */
  u16 temporary_service_id;

  /* 0 ok, 1 authentication failure, 2 class of service failure. */
  u8 response;

  u8 n_fragments;

  u8 this_fragment_sequence_number;

  docsis_tlv_t tlvs[0];
}) docsis_multipart_registration_response_t;

#define DOCSIS_TLV_VALID_IN_CONFIG_FILE (1 << 0)
#define DOCSIS_TLV_VALID_IN_REGISTRATION (1 << 1)
#define DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP (1 << 2)
#define DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE (1 << 3)

#define foreach_docsis_tlv_type						\
  _ (0, pad, 0,								\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (1, downstream_frequency, 4,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (2, upstream_channel_id, 1,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (3, cpe_network_access_allowed, 1,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (4, docsis_1_class_of_service, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (5, cable_modem_capabilities, 0,					\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (6, cable_modem_message_integrity_check, 16,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (7, cmts_message_integrity_check, 16,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (8, vendor_id, 3,							\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (9, software_upgrade_filename, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (10, snmp_write_access_control, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (11, snmp_mib_object, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (12, modem_ip4_address, 4,						\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (13, service_not_available_response, 3,				\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (14, cpe_ethernet_address, 6,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (15, telephone_settings_option_deprecated, 0,			\
     0)									\
  _ (17, baseline_privacy, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (18, max_number_of_cpes, 1,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (19, tftp_server_timestamp_of_config_file, 4,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (20, tftp_server_provisioned_modem_ip4_address, 4,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (21, software_upgrade_tftp_server_ip4_address, 4,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (22, upstream_packet_classification, 0,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP)				\
  _ (23, downstream_packet_classification, 0,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP)				\
  _ (24, upstream_service_flow, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP)				\
  _ (25, downstream_service_flow, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (26, payload_header_suppression, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (27, hmac_digest, 20,						\
     DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (28, maximum_number_of_classifiers, 2,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (29, privacy_enable, 1,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (30, authorization_block, 0,					\
     DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP)				\
  _ (31, key_sequence_number, 1,					\
     DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (32, manufacturer_code_verification_certificate, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (33, co_signer_code_verification_certificate, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (34, snmpv3_kickstart_value, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (35, subscriber_management_control, 3,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (36, subscriber_management_cpe_ip4_address_list, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (37, subscriber management_filter_groups, 8,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (38, snmpv3_notification_receiver, 0,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (39, enable_docsis_2_mode, 1,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (40, enable_test_modes, 1,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (41, downstream_channel_list, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (42, static_multicast_mac_address, 6,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (43, docsis_extension, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (44, vendor_specific_capabilities, 0,				\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (45, downstream_unencrypted_traffic_filtering, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (46, tx_channel_configuration, 0,					\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (47, service_flow_service_id_cluster_assignment, 0,			\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP \
     | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE)			\
  _ (48, rx_channel_profile, 0,						\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (49, rx_channel_config, 0,						\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (50, dsid_encodings, 0,						\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (51, security_association_encoding, 0,				\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (52, initializing_channel_timeout, 2,				\
     DOCSIS_TLV_VALID_IN_REGISTRATION | DOCSIS_TLV_VALID_IN_DYNAMIC_BONDING_CHANGE) \
  _ (53, snmpv1v2c_coexistence, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (54, snmpv3_access_view, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (55, snmp_cpe_access_enable, 1,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (56, channel_assignment, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (57, modem_initialization_reason, 1,				\
     DOCSIS_TLV_VALID_IN_REGISTRATION)					\
  _ (58, software_upgrade_tftp_server_ip6_address, 16,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (59, tftp_server_provisioned_modem_ip6_address, 16,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (60, upstream_drop_packet_classification, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION	\
     | DOCSIS_TLV_VALID_IN_DYNAMIC_SERVICE_OP)				\
  _ (61, subscriber_management_cpe_ip6_prefix_list, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (62, upstream_drop_classifier_group_id, 0,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (63, subscriber_management_control_max_cpe_ip6_addresses, 0,	\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (64, cmts_static_multicast_session_encoding, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (65, l2vpn_mac_aging_encoding, 0,					\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (66, management_event_control_encoding, 0,				\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)					\
  _ (67, subscriber_management_cpe_ip6_address_list, 0,			\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE | DOCSIS_TLV_VALID_IN_REGISTRATION) \
  _ (255, end_of_data, 0,						\
     DOCSIS_TLV_VALID_IN_CONFIG_FILE)

/* sub-tlvs of docsis_1_class_of_service */
#define foreach_docsis_class_of_service_tlv_type	\
  _ (invalid, 0)					\
  _ (class_id, 1)					\
  _ (max_downstream_bits_per_sec, 4)			\
  _ (max_upstream_bits_per_sec, 4)			\
  _ (priority, 1)					\
  _ (guaranteed_min_upstream_bits_per_sec, 4)		\
  _ (max_upstream_burst_bytes, 2)			\
  _ (privacy_enable, 1)

/* sub-tlvs for docsis_extension */
#define foreach_docsis_extension_tlv_type	\
  _ (invalid, 0)				\
  _ (load_balencing_policy_id, 4)		\
  _ (load_balencing_priority, 4)		\
  _ (load_balencing_group_id, 4)		\
  _ (ranging_class_id_extension, 4)		\
  _ (l2vpn_tlvs, 0)				\
  _ (extended_cmts_mic_config_tlvs, 0)		\
  _ (src_ip_address_verification_tlvs, 0)	\
  _ (reserved, 0)				\
  _ (cable_modem_attribute_mask, 0)

#define foreach_docsis_extension_extended_cmts_mic_tlv_type	\
  _ (invalid, 0)						\
  _ (mac_type, 1)						\
  _ (include_in_mac_bitmap, 0)					\
  _ (mac_value, 0)

#define foreach_docsis_extension_src_ip_address_verification_tlv_type	\
  _ (invalid, 0)							\
  _ (group_name, 0)							\
  _ (address_and_length, 0)

#define foreach_docsis_extension_src_ip_address_verification_address_and_length_tlv_type \
  _ (invalid, 0)							\
  _ (prefix, 0)								\
  _ (prefix_len, 1)

/* sub-tlvs for {down,up}stream_service_flow. */
#define foreach_docsis_service_flow_tlv_type				\
  _ (reference_number, 1, 2)						\
  _ (service_flow_id, 2, 4)						\
  _ (service_id, 3, 2)							\
  _ (class_name, 4, 0)		/* null terminated string */		\
  _ (error, 5, 0)							\
  _ (qos_parameter_set_type, 6, 1)					\
  _ (priority, 7, 1)		/* 0-7 */				\
  _ (max_sustained_rate_bits_per_sec, 8, 4) /* policer */		\
  _ (max_traffic_burst_in_bytes, 9, 4)					\
  _ (min_guaranteed_rate_bits_per_sec, 10, 4)				\
  _ (min_guaranteed_rate_packet_size, 11, 2)				\
  _ (active_qos_parameter_timeout_in_sec, 12, 2)			\
  _ (admitted_qos_parameter_timeout_in_sec, 13, 2)			\
  _ (peak_traffic_rate_bits_per_sec, 27, 4)				\
  _ (required_attribute_mask, 31, 4)					\
  _ (forbidden_attribute_mask, 32, 4)					\
  _ (attribute_aggregation_mask, 33, 4)					\
  _ (application_id, 34, 4)						\
  _ (buffer_control, 35, 0)						\
  _ (vendor_qos_param, 43, 0)

/* sub-tlvs for upstream_service_flow. */
#define foreach_docsis_upstream_service_flow_tlv_type			\
  _ (max_concatenated_burst, 14, 2)					\
  _ (scheduling_type, 15, 1)						\
  _ (request_tx_policy, 16, 4)						\
  _ (nominal_request_polling_interval_in_usec, 17, 4)			\
  _ (tolerated_request_polling_jitter_in_usec, 18, 4)			\
  _ (unsolicited_grant_size_in_bytes, 19, 2)				\
  _ (nominal_grant_interval_in_usec, 20, 4)				\
  _ (tolerated_grant_jitter_in_usec, 21, 4)				\
  _ (grants_per_interval, 22, 1)					\
  _ (ip_tos_overwrite, 23, 2)						\
  _ (unsolicited_grant_time_reference, 24, 4)				\
  _ (contention_request_backoff_window_multiplier_in_eights, 25, 1)	\
  _ (request_bytes_per_unit, 26, 1)

/* sub-tlvs for downstream_service_flow. */
#define foreach_docsis_downstream_service_flow_tlv_type	\
  /* downstream only */					\
  _ (max_downstream_latency, 14, 4)			\
  _ (downstream_resequancing, 17, 1)

#define foreach_docsis_payload_header_suppression_tlv_type	\
  _ (classifier_reference, 1, 1)				\
  _ (classifier_id, 2, 2)					\
  _ (service_flow_reference, 3, 2)				\
  _ (service_flow_id, 4, 4)					\
  _ (dynamic_service_change_action, 5, 1)			\
  _ (error_encodings, 6, 0)					\
  _ (supressed_header, 7, 0)					\
  _ (index, 8, 1)						\
  _ (mask_bitmap, 9, 0)						\
  _ (n_bytes_in_supressed_header, 10, 1)			\
  _ (verify, 11, 1)						\
  _ (dynamic_bonding_change_action, 13, 1)			\
  _ (vendor_specific, 43, 0)

typedef CLIB_PACKED (struct {
  u16 transaction_id;
  u8 confirmation_code;
  docsis_tlv_t tlvs[0];
}) docsis_transaction_response_t;

typedef struct {
  u16 transaction_id;
  u8 confirmation_code;
  u8 reserved;
} docsis_transaction_response_no_tlv_t;

typedef CLIB_PACKED (struct {
  u16 transaction_id;
  docsis_tlv_t tlvs[0];
}) docsis_transaction_request_t;

typedef struct {
  u16 transaction_id;
  u8 reserved[2];
  u32 service_flow_id;
  docsis_tlv_t tlvs[0];
} docsis_transaction_request_with_service_flow_t;

typedef struct {
  u16 transaction_id;
  u8 n_fragments;
  u8 this_fragment_sequence_number;
  docsis_tlv_t tlvs[0];
} docsis_fragmented_transaction_request_t;

typedef struct {
  u16 transaction_id;
  u8 downstream_channel_id;
  u8 flags;

  u16 average_internal;
  u8 start_point, end_point;

  /* start/end timestamps. */
  u32 timestamps[2];
} docsis_path_verify_request_t;

typedef struct {
  u16 transaction_id;
  u8 downstream_channel_id;
  u8 flags;

  u32 upstream_service_flow_id;

  u16 average_internal;
  u8 start_point, end_point;

  /* start/end timestamps. */
  u32 timestamps[2];
} docsis_path_verify_response_t;

#define foreach_docsis_cable_modem_status_event		\
  _ (invalid)						\
  _ (secondary_channel_mdd_timeout)			\
  _ (qam_fec_lock_failure)				\
  _ (sequence_number_out_of_range)			\
  _ (secondary_channel_mdd_recovery)			\
  _ (qam_fec_lock_recovery)				\
  _ (t4_timeout)					\
  _ (t3_retries_exceeded)				\
  _ (successfull_ranging_after_t3_retries_exceeded)	\
  _ (cable_modem_operating_on_battery_backup)		\
  _ (cable_modem_returned_to_ac_power)

typedef enum {
#define _(f) DOCSIS_CABLE_MODEM_STATUS_EVENT_##f,
  foreach_docsis_cable_modem_status_event
#undef _
} docsis_cable_modem_status_event_t;

typedef CLIB_PACKED (struct {
  u16 transaction_id;

  docsis_cable_modem_status_event_t event_code : 8;

  docsis_tlv_t tlvs[0];
}) docsis_cable_modem_status_report_t;

#endif /* included_docsis_packet_h */
