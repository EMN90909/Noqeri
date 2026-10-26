// SPDX-License-Identifier: Apache-2.0
#ifndef NOQERI_OWNED_VALUE_LIST_H
#define NOQERI_OWNED_VALUE_LIST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Clone into uninitialized, correctly aligned destination; return 0 on
// success. On failure the destination must remain unconstructed.
// Drop destroys a successfully cloned element. Callbacks must not reenter
// the owned-list runtime or throw through the C ABI.
typedef int32_t (*nq_value_clone_fn)(void* dest, const void* src, void* ctx);
typedef void (*nq_value_drop_fn)(void* value, void* ctx);
// For Noqeri's Copy types, use raw bit-copy and no finalizer.
// Nontrivial values require BOTH clone and drop callbacks.
int64_t nq_value_list_new_copy(uint64_t element_size,uint64_t alignment);
int64_t nq_value_list_new_custom(uint64_t element_size,uint64_t alignment,
                                 nq_value_clone_fn clone,nq_value_drop_fn drop,void* ctx);
int64_t nq_value_list_append(int64_t handle,const void* element);
int64_t nq_value_list_get(int64_t handle,int64_t index,void* output);
int64_t nq_value_list_remove(int64_t handle,int64_t index);
int64_t nq_value_list_size(int64_t handle);
int64_t nq_value_list_clear(int64_t handle);
int64_t nq_value_list_close(int64_t handle);
// Status: 0 success; -1 invalid handle, -2 allocation/capacity,
// -3 out-of-range, -5 invalid descriptor or aliased pointer,
// -6 user callback failure. get() clones to an UNINITIALIZED output
// and the caller owns/drop()s that independent clone if custom.
// Passing pointers to live elements of this same list is forbidden.
#ifdef __cplusplus
}
#endif
#endif
