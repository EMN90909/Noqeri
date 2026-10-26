// SPDX-License-Identifier: Apache-2.0
#ifndef NOQERI_OWNED_COLLECTIONS_H
#define NOQERI_OWNED_COLLECTIONS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Host-owned, dynamically growing int collections (opaque generational IDs).
// kind: 1=list, 2=map, 3=set. All statuses >=0 successful;
// -1 bad handle/kind, -2 capacity/allocation, -3 invalid index, -4 missing key.
int64_t nq_collection_new(int64_t kind);
int64_t nq_collection_add(int64_t handle,int64_t key,int64_t value);
int64_t nq_collection_get(int64_t handle,int64_t key,int64_t* output);
int64_t nq_collection_set(int64_t handle,int64_t key,int64_t value);
int64_t nq_collection_pop(int64_t handle,int64_t* output);
int64_t nq_collection_size(int64_t handle);
int64_t nq_collection_remove(int64_t handle,int64_t key);
int64_t nq_collection_clear(int64_t handle);
int64_t nq_collection_close(int64_t handle);
// Compiler-generated, checked List<int> hooks. On allocation/capacity or
// invalid-ownership failure these abort with a diagnostic instead of silently
// returning an incorrect value. List handles never alias ordinary integers.
int64_t nq_list_new(void);
int64_t nq_list_append(int64_t handle,int64_t value);
int64_t nq_list_length(int64_t handle);
int64_t nq_list_at(int64_t handle,int64_t index);
int64_t nq_list_set(int64_t handle,int64_t index,int64_t value);
int64_t nq_list_pop(int64_t handle);
int64_t nq_list_clear(int64_t handle);
int64_t nq_list_close(int64_t handle);
#ifdef __cplusplus
}
#endif
#endif
