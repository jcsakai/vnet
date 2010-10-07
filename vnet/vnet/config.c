/*
 * config.c: feature configuration
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

#include <vnet/vnet/config.h>

void vnet_config_init (vnet_config_main_t * cm,
		       u32 main_node_index,
		       u32 * feature_node_indices,
		       u32 n_features)
{
  memset (cm, 0, sizeof (cm[0]));

  /* Allocate null config which will never be deleted. */
  {
    vnet_config_t * null;

    pool_get (cm->config_pool, null); 

    ASSERT (null - cm->config_pool == 0);
    memset (null, 0, sizeof (null[0]));
  }

  cm->main_node_index = main_node_index;

  mhash_init_vec_string (&cm->config_string_hash, sizeof (uword));

  vec_add (cm->node_index_by_feature_index, feature_node_indices, n_features);
}

static vnet_config_feature_t *
duplicate_feature_vector (vnet_config_feature_t * feature_vector)
{
  vnet_config_feature_t * result, * f;

  result = vec_dup (feature_vector);
  vec_foreach (f, result)
    f->feature_config = vec_dup (f->feature_config);

  return result;
}

static void
free_feature_vector (vnet_config_feature_t * feature_vector)
{
  vnet_config_feature_t * f;

  vec_foreach (f, feature_vector)
    vnet_config_feature_free (f);
  vec_free (feature_vector);
}

static vnet_config_t *
find_config_with_features (vlib_main_t * vm,
			   vnet_config_main_t * cm,
			   vnet_config_feature_t * feature_vector)
{
  u32 last_node_index = cm->main_node_index;
  vnet_config_feature_t * f;
  u8 * config_string = 0;
  uword * p;
  vnet_config_t * result;

  vec_foreach (f, feature_vector)
    {
      /* Connect node graph. */
      f->next_index = vlib_node_add_next (vm, last_node_index, f->node_index);
      last_node_index = f->node_index;

      /* Store next index in config string. */
      ASSERT (f->next_index < (1 << BITS (config_string[0])));
      vec_add1 (config_string, f->next_index);

      /* Store feature config. */
      vec_add (config_string, f->feature_config, vec_len (f->feature_config));
    }

  /* See if config string is unique. */
  p = mhash_get (&cm->config_string_hash, config_string);
  if (p)
    {
      /* Not unique.  Share existing config. */
      vec_free (config_string);
      free_feature_vector (feature_vector);
      result = pool_elt_at_index (cm->config_pool, p[0]);
    }
  else
    {
      pool_get (cm->config_pool, result);
      result->index = result - cm->config_pool;
      result->features = feature_vector;
      result->buffer_config = config_string;
      result->reference_count = 0; /* will be incremented by caller. */
      mhash_set (&cm->config_string_hash, config_string, result->index,
		 /* old_value */ 0);
    }

  return result;
}

static void
remove_reference (vnet_config_main_t * cm, vnet_config_t * c)
{
  ASSERT (c->reference_count > 0);
  ASSERT (c->index != 0);
  c->reference_count -= 1;
  if (c->reference_count == 0)
    {
      mhash_unset (&cm->config_string_hash, c->buffer_config, /* old_value */ 0);
      vnet_config_free (c);
      pool_put (cm->config_pool, c);
    }
}

u32 vnet_config_add_feature (vlib_main_t * vm,
			     vnet_config_main_t * cm,
			     u32 config_id,
			     u32 feature_index,
			     void * feature_config,
			     u32 n_feature_config_bytes)
{
  vnet_config_t * old, * new;
  vnet_config_feature_t * new_features, * f;

  old = pool_elt_at_index (cm->config_pool, config_id);
  new_features = old->features;
  if (new_features)
    new_features = duplicate_feature_vector (new_features);

  vec_add2 (new_features, f, 1);
  f->feature_index = feature_index;
  f->node_index = vec_elt (cm->node_index_by_feature_index, feature_index);
  vec_add (f->feature_config, feature_config, n_feature_config_bytes);
  
  /* Sort (prioritize) features. */
  if (vec_len (new_features) > 1)
    vec_sort (new_features, f1, f2, (int) f2->feature_index - f1->feature_index);

  new = find_config_with_features (vm, cm, new_features);

  if (old->index != 0)
    remove_reference (cm, old);

  new->reference_count += 1;
  return new->index;
}

u32 vnet_config_del_feature (vlib_main_t * vm,
			     vnet_config_main_t * cm,
			     u32 config_id,
			     u32 feature_index,
			     void * feature_config,
			     u32 n_feature_config_bytes)
{
  vnet_config_t * old, * new;
  vnet_config_feature_t * new_features, * f;

  ASSERT (config_id != 0);
  if (config_id == 0)
    return ~0;

  old = pool_elt_at_index (cm->config_pool, config_id);

  /* Find feature with same index and opaque data. */
  vec_foreach (f, old->features)
    {
      if (f->feature_index == feature_index
	  && vec_len (f->feature_config) == n_feature_config_bytes
	  && (n_feature_config_bytes == 0
	      || ! memcmp (f->feature_config, feature_config, n_feature_config_bytes)))
	break;
    }

  /* Feature not found. */
  if (f >= vec_end (old->features))
    return ~0;

  new_features = duplicate_feature_vector (old->features);
  f = new_features + (f - old->features);
  vnet_config_feature_free (f);
  vec_delete (new_features, 1, f - new_features);

  /* New config is now empty? */
  if (vec_len (new_features) == 0)
    {
      vec_free (new_features);
      new = pool_elt_at_index (cm->config_pool, 0);
    }
  else
    {
      new = find_config_with_features (vm, cm, new_features);
      new->reference_count += 1;
    }

  if (new->index != 0)
    new->reference_count += 1;

  remove_reference (cm, old);

  return new->index;
}
