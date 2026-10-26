// SPDX-License-Identifier: GPL-3.0-only
// NoeSQL session_v2 ABI mirrors the canonical Rust NoeSqlSessionV2 callback table.
// The provider supplies actual query/transaction behavior; this file does not
// implement NoeSQL storage or translate NoeSQL source into another SQL dialect.
#pragma once
#include <cstddef>
#include <cstdint>
struct NqNoeSqlSessionV2 {
    std::uint32_t abi_version;
    void* context;
    int (*open)(void*, const std::uint8_t*,std::size_t,void**);
    int (*close)(void*,void*);
    int (*prepare)(void*,void*,const std::uint8_t*,std::size_t,void**);
    int (*release)(void*,void*,void*);
    int (*execute)(void*,void*,void*,const std::uint8_t*,std::size_t,std::uint8_t*,std::size_t,std::size_t*);
    int (*query)(void*,void*,void*,const std::uint8_t*,std::size_t,std::uint8_t*,std::size_t,std::size_t*);
    int (*begin)(void*,void*);
    int (*commit)(void*,void*);
    int (*rollback)(void*,void*);
    int (*last_error)(void*,void*,std::uint8_t*,std::size_t,std::size_t*);
};
using NqNoeSqlSessionGetter = const NqNoeSqlSessionV2* (*)();
// Provider must export extern "C" const NqNoeSqlSessionV2* noesql_session_v2_get();
// NoeSQL bindings use UTF-8 JSON object bytes { "name": "value" } on this
// Noqeri-specific adapter profile. Native Rust provider readiness is separate.
