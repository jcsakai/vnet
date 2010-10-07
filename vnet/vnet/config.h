/*
 * config.h: feature configuration
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

#ifndef included_vnet_config_h
#define included_vnet_config_h

#include <vlib/vlib.h>

typedef struct {
  /* Features are prioritized by index.  Smaller indices get
     performed first. */
  u32 feature_index;

  /* VLIB node which performs feature. */
  u32 node_index;

  /* Next index relative to previous node or main node. */
  u32 next_index;

  /* Opaque per feature configuration data. */
  u8 * feature_config;
} vnet_config_feature_t;

always_inline void
vnet_config_feature_free (vnet_config_feature_t * f)
{ vec_free (f->feature_config); }

typedef struct {
  /* Sorted vector of features for this configuration. */
  vnet_config_feature_t * features;

  /* Config string for placing in VLIB buffers. */
  u8 * buffer_config;

  /* Index in main pool. */
  u32 index;

  /* Number of interfaces/traffic classes that reference this config. */
  u32 reference_count;
} vnet_config_t;

always_inline void
vnet_config_free (vnet_config_t * c)
{
  vnet_config_feature_t * f;
  vec_foreach (f, c->features)
    vnet_config_feature_free (f);
  vec_free (c->features);
  vec_free (c->buffer_config);
}

typedef struct {
  /* Pool of configs.  Index 0 is always null config and is never deleted. */
  vnet_config_t * config_pool;

  /* Node index which starts feature processing. */
  u32 main_node_index;

  u32 * node_index_by_feature_index;

  mhash_t config_string_hash;
} vnet_config_main_t;

void vnet_config_init (vnet_config_main_t * cm,
		       u32 main_node_index,
		       u32 * feature_node_indices,
		       u32 n_features);

/* Calls to add/delete features from configurations. */
u32 vnet_config_add_feature (vlib_main_t * vm,
			     vnet_config_main_t * cm,
			     u32 config_id,
			     u32 feature_index,
			     void * feature_config,
			     u32 n_feature_config_bytes);

u32 vnet_config_del_feature (vlib_main_t * vm,
			     vnet_config_main_t * cm,
			     u32 config_id,
			     u32 feature_index,
			     void * feature_config,
			     u32 n_feature_config_bytes);

#endif /* included_vnet_config_h */
