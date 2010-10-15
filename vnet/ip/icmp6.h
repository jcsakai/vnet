#ifndef included_vnet_icmp6_h
#define included_vnet_icmp6_h

#define foreach_icmp6_error						\
  _ (NONE, "valid packets")						\
  _ (UNKNOWN_TYPE, "unknown type")					\
  _ (INVALID_CODE_FOR_TYPE, "invalid code for type")			\
  _ (INVALID_HOP_LIMIT_FOR_TYPE, "hop_limit != 255")			\
  _ (LENGTH_TOO_SMALL_FOR_TYPE, "payload length too small for type")	\
  _ (OPTIONS_WITH_ODD_LENGTH, "total option length not multiple of 8 bytes") \
  _ (OPTION_WITH_ZERO_LENGTH, "option has zero length")			\
  _ (ECHO_REPLIES_SENT, "echo replies sent")				\
  _ (NEIGHBOR_SOLICITATION_SOURCE_NOT_ON_LINK, "neighbor solicitations from source not on link") \
  _ (NEIGHBOR_SOLICITATION_SOURCE_UNKNOWN, "neighbor solicitations for unknown targets") \
  _ (NEIGHBOR_ADVERTISEMENTS_SENT, "neighbor advertisements sent")

typedef enum {
#define _(f,s) ICMP6_ERROR_##f,
  foreach_icmp6_error
#undef _
} icmp6_error_t;

typedef struct {
  u8 packet_data[64];
} icmp6_input_trace_t;

format_function_t format_icmp6_input_trace;
void icmp6_register_type (vlib_main_t * vm, icmp6_type_t type, u32 node_index);

extern vlib_node_registration_t ip6_icmp_input_node;

#endif /* included_vnet_icmp6_h */


