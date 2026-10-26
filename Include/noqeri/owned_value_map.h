// SPDX-License-Identifier: Apache-2.0
#ifndef NOQERI_OWNED_VALUE_MAP_H
#define NOQERI_OWNED_VALUE_MAP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Hosted dynamically allocated COPY-only map/set ABI. Keys use canonical
// integer object representations (Noqeri facade enforces Integer).
// No pointer ownership, callbacks, or structural key hashing is implied.
int64_t nq_value_map_new_copy(uint64_t key_size,uint64_t key_align,uint64_t value_size,uint64_t value_align);
int64_t nq_value_map_put(int64_t handle,const void* key,const void* value);
int64_t nq_value_map_get(int64_t handle,const void* key,void* out);
int64_t nq_value_map_remove(int64_t handle,const void* key);
int64_t nq_value_map_size(int64_t handle);
int64_t nq_value_map_close(int64_t handle);
int64_t nq_value_set_new_copy(uint64_t value_size,uint64_t alignment);
int64_t nq_value_set_add(int64_t handle,const void* value);
int64_t nq_value_set_has(int64_t handle,const void* value);
int64_t nq_value_set_remove(int64_t handle,const void* value);
int64_t nq_value_set_size(int64_t handle);
int64_t nq_value_set_close(int64_t handle);
// Status: 0 success; -1 bad handle; -2 capacity or allocation;
// -4 key absent; -5 invalid type, input or operation.
// set_has returns 1 for present, 0 for absent, negative for error.
#ifdef __cplusplus
}
#endif
#endif
