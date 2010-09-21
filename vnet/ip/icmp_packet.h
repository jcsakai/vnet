#ifndef included_vnet_icmp_h
#define included_vnet_icmp_h

#define foreach_icmp_type			\
  _ (0, echo_reply)				\
  _ (3, destination_unreachable)		\
  _ (4, source_quench)				\
  _ (5, redirect)				\
  _ (6, alternate_host_address)			\
  _ (8, echo_request)				\
  _ (9, router_advertisement)			\
  _ (10, router_solicitation)			\
  _ (11, time_exceeded)				\
  _ (12, parameter_problem)			\
  _ (13, timestamp_request)			\
  _ (14, timestamp_reply)			\
  _ (15, information_request)			\
  _ (16, information_reply)			\
  _ (17, address_mask_request)			\
  _ (18, address_mask_reply)			\
  _ (30, traceroute)				\
  _ (31, datagram_conversion_error)		\
  _ (32, mobile_host_redirect)			\
  _ (33, ip6_where_are_you)			\
  _ (34, ip6_i_am_here)				\
  _ (35, mobile_registration_request)		\
  _ (36, mobile_registration_reply)		\
  _ (37, domain_name_request)			\
  _ (38, domain_name_reply)			\
  _ (39, skip)					\
  _ (40, photuris)

#define icmp_no_code 0

#define foreach_icmp_code						\
  _ (destination_unreachable, 0, destination_unreachable_net)		\
  _ (destination_unreachable, 1, destination_unreachable_host)		\
  _ (destination_unreachable, 2, protocol_unreachable)			\
  _ (destination_unreachable, 3, port_unreachable)			\
  _ (destination_unreachable, 4, fragmentation_needed_and_dont_fragment_set) \
  _ (destination_unreachable, 5, source_route_failed)			\
  _ (destination_unreachable, 6, destination_network_unknown)		\
  _ (destination_unreachable, 7, destination_host_unknown)		\
  _ (destination_unreachable, 8, source_host_isolated)			\
  _ (destination_unreachable, 9, network_administratively_prohibited)	\
  _ (destination_unreachable, 10, host_administratively_prohibited)	\
  _ (destination_unreachable, 11, network_unreachable_for_type_of_service) \
  _ (destination_unreachable, 12, host_unreachable_for_type_of_service)	\
  _ (destination_unreachable, 13, communication_administratively_prohibited) \
  _ (destination_unreachable, 14, host_precedence_violation)		\
  _ (destination_unreachable, 15, precedence_cutoff_in_effect)		\
  _ (redirect, 0, network_redirect)					\
  _ (redirect, 1, host_redirect)					\
  _ (redirect, 2, type_of_service_and_network_redirect)			\
  _ (redirect, 3, type_of_service_and_host_redirect)			\
  _ (router_advertisement, 0, normal_router_advertisement)		\
  _ (router_advertisement, 16, does_not_route_common_traffic)		\
  _ (time_exceeded, 0, ttl_exceeded_in_transit)				\
  _ (time_exceeded, 1, fragment_reassembly_time_exceeded)		\
  _ (parameter_problem, 0, pointer_indicates_error)			\
  _ (parameter_problem, 1, missing_required_option)			\
  _ (parameter_problem, 2, bad_length)

typedef enum {
#define _(n,f) ICMP_##f = n,
  foreach_icmp_type
#undef _
} icmp_type_t;

typedef enum {
#define _(t,n,f) ICMP_##t##_##f = n,
  foreach_icmp_code
#undef _
} icmp_code_t;

typedef PACKED (struct {
  u8 type;

  u8 code;

  /* IP checksum of icmp header plus data which follows. */
  u16 checksum;
}) icmp_header_t;

#endif /* included_vnet_icmp_h */
