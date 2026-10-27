cmake_minimum_required(VERSION 3.16)
project(noqeri VERSION 0.1.4 LANGUAGES CXX)
include(GNUInstallDirs)
option(NOQERI_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NOQERI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(DEFINED CMAKE_BUILD_TYPE AND CMAKE_CONFIGURATION_TYPES)
 message(STATUS "Multi-config generator detected; cmake --build --config selects the active configuration (${CMAKE_BUILD_TYPE}).")
endif()
# C++ is restricted to the compiler/bootstrap and public embedding ABI layer.
# Reusable language-facing modules, parser helpers, runtime helpers, platform
# policy, security and data structures live in .noqeri. Bootstrap C++ remains until
# the Noqeri compiler can compile the corresponding compiler/toolchain pieces.
set(NOQERI_FRONTEND_SOURCES
 Compiler/core.cpp Compiler/modules.cpp Compiler/workspace.cpp Compiler/type_checker.cpp
 Compiler/lifetime.cpp Compiler/safety.cpp Compiler/generics.cpp Compiler/nir.cpp Compiler/nir_analysis.cpp Compiler/register_alloc.cpp Compiler/optimizer.cpp
 Compiler/bootstrap/lexer_host.cpp Compiler/bootstrap/parser_host.cpp Compiler/bootstrap/abi_runtime_host.cpp Compiler/bootstrap/interpreter_host.cpp
 Compiler/bootstrap/noqeridb_host.cpp)
set(NOQERI_SOURCES ${NOQERI_FRONTEND_SOURCES}
 Compiler/native_backend.cpp Compiler/web_backend.cpp Compiler/nqri_backend.cpp Compiler/shader_backend.cpp Compiler/linker.cpp
 Compiler/bootstrap/cli_host.cpp Compiler/bootstrap/formatter_host.cpp Compiler/bootstrap/package_host.cpp Compiler/bootstrap/lsp_host.cpp
 Compiler/bootstrap/test_runner_host.cpp Compiler/bootstrap/production_doctor_host.cpp
 Compiler/bootstrap/security_audit_host.cpp)
add_executable(noqeri ${NOQERI_SOURCES})
add_executable(noqeri-nqri Runtime/native/nqri_vm.cpp)
# Hosted, heap-backed int list/map/set ABI for ordinary Noqeri native AOT.
# Opaque handles are validated; allocation is bounded and explicit close
# releases ownership. Does not change freestanding bootstrap semantics.
add_library(noqeri_owned_collections STATIC Runtime/native/owned_collections.cpp Runtime/native/owned_value_list.cpp Runtime/native/owned_value_map.cpp)
target_include_directories(noqeri_owned_collections PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include/noqeri>
 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/noqeri>)
target_compile_features(noqeri_owned_collections PUBLIC cxx_std_17)

# Lightweight AI: first-party Apache library and GPL CLI host.
# All training and inference run locally on the CPU without Python/Node or
# heavyweight ML/LLM dependencies. No autonomous shell/network privileges.
add_library(noqeri_ai STATIC Runtime/native/ai_runtime.cpp Runtime/native/lm_runtime.cpp
 Runtime/native/ai_framework_tensor.cpp Runtime/native/ai_framework_transformer.cpp
 Runtime/native/ai_optimizer.cpp
 Runtime/native/ai_framework_c.cpp Runtime/native/ai_adamw_c.cpp Runtime/native/matrix.cpp
  Runtime/native/matrix_advanced.cpp Runtime/native/ai_matrix_c.cpp)
target_include_directories(noqeri_ai PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(noqeri_ai PUBLIC cxx_std_17)
if(BUILD_TESTING)
 add_executable(noqeri-matrix-test tests/cpp/matrix_test.cpp)
  add_executable(noqeri-matrix-advanced-test tests/cpp/matrix_advanced_test.cpp)
  target_link_libraries(noqeri-matrix-advanced-test PRIVATE noqeri_ai)
  target_compile_features(noqeri-matrix-advanced-test PRIVATE cxx_std_17)
  add_test(NAME noqeri-matrix-advanced COMMAND noqeri-matrix-advanced-test)
 target_link_libraries(noqeri-matrix-test PRIVATE noqeri_ai)
 add_test(NAME noqeri-matrix COMMAND noqeri-matrix-test)
endif()
add_executable(noqeri-ai Tools/ai/noqeri_ai.cpp)
target_link_libraries(noqeri-ai PRIVATE noqeri_ai)
target_compile_features(noqeri-ai PRIVATE cxx_std_17)

# Portable native web static compiler/preview server: no Node.js or JavaScript tooling.
add_executable(noqeri-web Compiler/bootstrap/web_native.cpp)
target_compile_features(noqeri-web PRIVATE cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(noqeri-web PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
# The AOT bundler embeds the GPL bootstrap runtime when producing a standalone
# app. Package these sources for installed compiler users; no Node.js required.
install(FILES
 Compiler/bootstrap/web_native.cpp
 Compiler/bootstrap/web_native_app.hpp
 Compiler/bootstrap/web_native_db.hpp
 Compiler/bootstrap/web_native_cloud.hpp
 Compiler/bootstrap/web_native_bundle.hpp
 Compiler/bootstrap/web_native_noesql_v2.hpp
 DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Compiler/bootstrap)
install(FILES LICENSES.md LICENSES/Apache-2.0.txt
 DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/licenses)
# Optional native SQL drivers. NoeSQL is separately built from EMN90909/NoeSQL;
# unavailable adapters fail explicitly (no engine or file format fallback).
option(NOQERI_WEB_SQL_DRIVERS "Enable discovered native SQL client libraries" ON)
if(NOQERI_WEB_SQL_DRIVERS)
 find_package(SQLite3 QUIET)
 if(SQLite3_FOUND)
  target_compile_definitions(noqeri-web PRIVATE NOQERI_WEB_SQLITE=1)
  target_link_libraries(noqeri-web PRIVATE SQLite::SQLite3)
 endif()
 find_package(PostgreSQL QUIET)
 if(PostgreSQL_FOUND)
  target_compile_definitions(noqeri-web PRIVATE NOQERI_WEB_POSTGRES=1)
  target_link_libraries(noqeri-web PRIVATE PostgreSQL::PostgreSQL)
 endif()
 find_path(NOQERI_WEB_DUCKDB_INCLUDE duckdb.h)
 find_library(NOQERI_WEB_DUCKDB_LIBRARY NAMES duckdb)
 if(NOQERI_WEB_DUCKDB_INCLUDE AND NOQERI_WEB_DUCKDB_LIBRARY)
  target_include_directories(noqeri-web PRIVATE ${NOQERI_WEB_DUCKDB_INCLUDE})
  target_link_libraries(noqeri-web PRIVATE ${NOQERI_WEB_DUCKDB_LIBRARY})
  target_compile_definitions(noqeri-web PRIVATE NOQERI_WEB_DUCKDB=1)
 endif()
 find_path(NOQERI_WEB_MYSQL_INCLUDE mysql.h PATH_SUFFIXES mariadb mysql)
 find_library(NOQERI_WEB_MYSQL_LIBRARY NAMES mariadb mysqlclient)
 if(NOQERI_WEB_MYSQL_INCLUDE AND NOQERI_WEB_MYSQL_LIBRARY)
  target_include_directories(noqeri-web PRIVATE ${NOQERI_WEB_MYSQL_INCLUDE})
  target_link_libraries(noqeri-web PRIVATE ${NOQERI_WEB_MYSQL_LIBRARY})
  target_compile_definitions(noqeri-web PRIVATE NOQERI_WEB_MYSQL=1)
 endif()
endif()

# Cloud HTTPS adapters are server-only and optional; they never embed API keys
# in browser modules or require Node.js.
option(NOQERI_WEB_CLOUD_DRIVERS "Enable native Supabase/Firebase HTTPS REST adapters" ON)
if(NOQERI_WEB_CLOUD_DRIVERS)
 find_package(CURL QUIET)
 if(CURL_FOUND)
  target_compile_definitions(noqeri-web PRIVATE NOQERI_WEB_CURL=1)
  target_link_libraries(noqeri-web PRIVATE CURL::libcurl)
 endif()
endif()
target_compile_features(noqeri-nqri PRIVATE cxx_std_17)
target_include_directories(noqeri PRIVATE Include/noqeri Compiler/bootstrap)
target_compile_features(noqeri PRIVATE cxx_std_17)
target_compile_definitions(noqeri PRIVATE NOQERI_BOOTSTRAP=1)
add_library(noqeri_abi SHARED ${NOQERI_FRONTEND_SOURCES})
target_include_directories(noqeri_abi PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include/noqeri> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/noqeri> PRIVATE Compiler/bootstrap)
target_compile_features(noqeri_abi PUBLIC cxx_std_17)
target_compile_definitions(noqeri_abi PRIVATE NOQERI_BOOTSTRAP=1)
set_source_files_properties(Compiler/bootstrap/noqeridb_host.cpp PROPERTIES COMPILE_DEFINITIONS NOQERI_DATABASE_SOURCE=1)
set_target_properties(noqeri_abi PROPERTIES OUTPUT_NAME noqeri-abi WINDOWS_EXPORT_ALL_SYMBOLS ON)
# Native owned collections always exercise the actual auto-growing C ABI.
include(CTest)
if(BUILD_TESTING)
 add_executable(noqeri-owned-collections-test tests/cpp/owned_collections_test.cpp)
 target_link_libraries(noqeri-owned-collections-test PRIVATE noqeri_owned_collections Threads::Threads)
 add_test(NAME noqeri-owned-collections COMMAND noqeri-owned-collections-test)
 add_executable(noqeri-owned-value-list-test tests/cpp/owned_value_list_test.cpp)
 target_link_libraries(noqeri-owned-value-list-test PRIVATE noqeri_owned_collections Threads::Threads)
 add_test(NAME noqeri-owned-value-list COMMAND noqeri-owned-value-list-test)
endif()
function(noqeri_apply_warnings target)
 if(MSVC)
  target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /wd4456 /wd4701)
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(NOQERI_WARNINGS_AS_ERRORS)
   target_compile_options(${target} PRIVATE /WX)
  endif()
 else()
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-misleading-indentation)
  if(NOQERI_WARNINGS_AS_ERRORS)
   target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(NOQERI_ENABLE_SANITIZERS)
   target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
   target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
 endif()
endfunction()
noqeri_apply_warnings(noqeri)
noqeri_apply_warnings(noqeri-nqri)
noqeri_apply_warnings(noqeri_ai)
noqeri_apply_warnings(noqeri-ai)
noqeri_apply_warnings(noqeri_owned_collections)
if(TARGET noqeri-owned-collections-test)
 noqeri_apply_warnings(noqeri-owned-collections-test)
endif()
noqeri_apply_warnings(noqeri-web)
noqeri_apply_warnings(noqeri_abi)
# Android app embedding: native compiler and interpreter run inside an APK
# via JNI. CLI main() is omitted so a shared library can safely expose the
# same compiler frontend to the mobile activity.
option(NOQERI_BUILD_ANDROID_APP "Build Android JNI compiler embedding" OFF)
if(ANDROID AND NOQERI_BUILD_ANDROID_APP)
 set(NOQERI_ANDROID_BRIDGE_SOURCES ${NOQERI_SOURCES})
 list(REMOVE_ITEM NOQERI_ANDROID_BRIDGE_SOURCES Compiler/bootstrap/cli_host.cpp)
 add_library(noqeri_android SHARED
  ${NOQERI_ANDROID_BRIDGE_SOURCES} AndroidApp/native_bridge.cpp)
 target_include_directories(noqeri_android PRIVATE Include/noqeri Compiler/bootstrap)
 target_compile_features(noqeri_android PRIVATE cxx_std_17)
 target_compile_definitions(noqeri_android PRIVATE NOQERI_BOOTSTRAP=1)
 noqeri_apply_warnings(noqeri_android)
endif()

# Standalone graphics compiler/renderer qualification avoids platform-specific
# mailbox/PGAS dependencies. Noqeri shader compilation still uses the REAL
# compiler executable and the same C++ shader backend on every host.
option(NOQERI_GRAPHICS_PORTABLE_BUILD
 "Configure shader compiler and graphics library without network/database services" OFF)
option(NOQERI_GRAPHICS_FORCE_SCALAR
 "Force portable scalar framebuffer clears (for architecture qualification)" OFF)
add_library(noqeri_graphics_fastpath STATIC Runtime/native/graphics_fastpath.cpp)
target_include_directories(noqeri_graphics_fastpath PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(noqeri_graphics_fastpath PUBLIC cxx_std_17)
if(NOQERI_GRAPHICS_FORCE_SCALAR)
 target_compile_definitions(noqeri_graphics_fastpath PRIVATE NOQERI_GRAPHICS_FORCE_SCALAR=1)
endif()
noqeri_apply_warnings(noqeri_graphics_fastpath)
install(TARGETS noqeri_graphics_fastpath ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
include(CTest)
if(BUILD_TESTING)
 add_executable(noqeri-ai-runtime-test tests/cpp/ai_runtime_test.cpp)
 target_link_libraries(noqeri-ai-runtime-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-ai-runtime-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-ai-runtime-test)
 add_test(NAME noqeri-ai-runtime COMMAND noqeri-ai-runtime-test ${CMAKE_BINARY_DIR}/ai-test-data)
 add_executable(noqeri-lm-runtime-test tests/cpp/lm_runtime_test.cpp)
 target_link_libraries(noqeri-lm-runtime-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-lm-runtime-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-lm-runtime-test)
 add_test(NAME noqeri-lm-runtime COMMAND noqeri-lm-runtime-test ${CMAKE_BINARY_DIR}/lm-test-data)
 add_executable(noqeri-ai-autodiff-deep-test tests/cpp/ai_autodiff_deep_test.cpp)
 target_link_libraries(noqeri-ai-autodiff-deep-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-ai-autodiff-deep-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-ai-autodiff-deep-test)
 add_test(NAME noqeri-ai-autodiff-deep COMMAND noqeri-ai-autodiff-deep-test)
 add_executable(noqeri-ai-optimizer-test tests/cpp/ai_optimizer_test.cpp)
 target_link_libraries(noqeri-ai-optimizer-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-ai-optimizer-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-ai-optimizer-test)
 add_test(NAME noqeri-ai-optimizer COMMAND noqeri-ai-optimizer-test)
 add_executable(noqeri-ai-framework-test tests/cpp/ai_framework_test.cpp)
 target_link_libraries(noqeri-ai-framework-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-ai-framework-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-ai-framework-test)
 add_test(NAME noqeri-ai-framework COMMAND noqeri-ai-framework-test ${CMAKE_BINARY_DIR}/ai-framework-test-data)
 add_executable(noqeri-adamw-c-test tests/cpp/ai_adamw_c_test.cpp)
 target_link_libraries(noqeri-adamw-c-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-adamw-c-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-adamw-c-test)
 add_test(NAME noqeri-adamw-c COMMAND noqeri-adamw-c-test)
 add_executable(noqeri-ai-framework-c-test tests/cpp/ai_framework_c_test.cpp)
 target_link_libraries(noqeri-ai-framework-c-test PRIVATE noqeri_ai)
 target_compile_features(noqeri-ai-framework-c-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-ai-framework-c-test)
 add_test(NAME noqeri-ai-framework-c COMMAND noqeri-ai-framework-c-test)
endif()
if(BUILD_TESTING)
 add_executable(noqeri-websocket-frame-test tests/cpp/websocket_frame_test.cpp)
 target_include_directories(noqeri-websocket-frame-test PRIVATE Include)
 target_compile_features(noqeri-websocket-frame-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-websocket-frame-test)
 add_test(NAME noqeri-websocket-frame COMMAND noqeri-websocket-frame-test)
endif()
if(BUILD_TESTING)
 add_executable(noqeri-graphics-portability-test tests/cpp/graphics_portability_test.cpp)
 target_link_libraries(noqeri-graphics-portability-test PRIVATE noqeri_graphics_fastpath)
 target_compile_features(noqeri-graphics-portability-test PRIVATE cxx_std_17)
 if(NOQERI_GRAPHICS_FORCE_SCALAR)
  target_compile_definitions(noqeri-graphics-portability-test PRIVATE NOQERI_GRAPHICS_FORCE_SCALAR=1)
 endif()
 noqeri_apply_warnings(noqeri-graphics-portability-test)
 add_test(NAME noqeri-graphics-portability COMMAND noqeri-graphics-portability-test)
 add_test(NAME noqeri-graphics-shader-portability
  COMMAND ${CMAKE_COMMAND}
   "-DNOQERI_COMPILER=$<TARGET_FILE:noqeri>"
   "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
   "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
   -P ${CMAKE_SOURCE_DIR}/tests/cmake/graphics_shader_portability.cmake)
endif()
# Optional cross-platform OS presentation bridge: only the SDL2 surface/window
# adapter is native. All raster algorithms live in Lib/std/render2d.noqeri.
# No SDL2 dependency is imposed on freestanding compiler/runtime targets.
option(NOQERI_ENABLE_SDL2 "Build optional Noqeri OS framebuffer SDL2 adapter" ON)
if(NOQERI_ENABLE_SDL2)
 find_package(SDL2 CONFIG QUIET)
 if(TARGET SDL2::SDL2)
  add_library(noqeri_os_renderer_sdl STATIC Runtime/native/os_renderer_sdl.cpp)
  target_include_directories(noqeri_os_renderer_sdl PUBLIC Include)
  target_link_libraries(noqeri_os_renderer_sdl PUBLIC SDL2::SDL2)
  target_compile_features(noqeri_os_renderer_sdl PUBLIC cxx_std_17)
  noqeri_apply_warnings(noqeri_os_renderer_sdl)
 endif()
endif()

install(TARGETS noqeri-ai RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS noqeri_ai ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Supply the project's GPL license within both archives and native installers.
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri)

# This opt-in mode isolates portable shader generation and native framebuffer
# runtime even on BSD/Android, where networking services differ. A full native
# Noqeri executable/linker for each target is a separate qualification gate.
if(NOQERI_GRAPHICS_PORTABLE_BUILD)
 # The portable profile deliberately omits Linux-specific mailbox/PGAS services,
 # but its installed SDK must still contain the bytecode VM, public headers,
 # and Noqeri-owned standard/runtime source modules.
 install(TARGETS noqeri noqeri-nqri noqeri-web RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
 install(DIRECTORY Include/noqeri/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/noqeri
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
 foreach(component IN ITEMS Lib Modules Objects Database Runtime Parser)
  install(DIRECTORY ${component}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/${component}
   FILES_MATCHING PATTERN "*.noqeri")
 endforeach()
 install(DIRECTORY Platforms/ PC/ Mac/ Android/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/platforms
  FILES_MATCHING PATTERN "*.noqeri")
 install(FILES Brand/noqeri-logo.webp Brand/noqeri-mark.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/brand)
 include(cmake/NoqeriPackaging.cmake)
 return()
endif()
if(BUILD_TESTING)
 add_executable(noqeri-phi-edge-emitter
   tests/cpp/phi_edge_codegen_test.cpp Compiler/native_backend.cpp)
 target_include_directories(noqeri-phi-edge-emitter PRIVATE
   Include/noqeri Compiler/bootstrap)
 target_compile_features(noqeri-phi-edge-emitter PRIVATE cxx_std_17)
 target_link_libraries(noqeri-phi-edge-emitter PRIVATE noqeri_abi)
 noqeri_apply_warnings(noqeri-phi-edge-emitter)
 add_executable(noqeri-simd-codegen-test
   tests/cpp/native_simd_emit_test.cpp Compiler/native_backend.cpp)
 target_include_directories(noqeri-simd-codegen-test PRIVATE
   Include/noqeri Compiler/bootstrap)
 target_compile_features(noqeri-simd-codegen-test PRIVATE cxx_std_17)
 target_link_libraries(noqeri-simd-codegen-test PRIVATE noqeri_abi)
 noqeri_apply_warnings(noqeri-simd-codegen-test)
 add_executable(noqeri-optimizer-test tests/cpp/optimizer_liveness_test.cpp Compiler/optimizer.cpp)
 add_executable(noqeri-register-allocator-test tests/cpp/register_allocator_cfg_test.cpp Compiler/register_alloc.cpp)
 target_include_directories(noqeri-register-allocator-test PRIVATE Include/noqeri Compiler/bootstrap)
 target_compile_features(noqeri-register-allocator-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-register-allocator-test)
 add_test(NAME noqeri-register-allocator-cfg COMMAND noqeri-register-allocator-test)
 target_include_directories(noqeri-optimizer-test PRIVATE Include/noqeri Compiler/bootstrap)
 target_compile_features(noqeri-optimizer-test PRIVATE cxx_std_17)
 noqeri_apply_warnings(noqeri-optimizer-test)
 add_test(NAME noqeri-optimizer-liveness COMMAND noqeri-optimizer-test)
 add_test(NAME noqeri-reject-incomplete-hex COMMAND noqeri check ${CMAKE_SOURCE_DIR}/tests/invalid_hex_literal.noqeri)
 set_tests_properties(noqeri-reject-incomplete-hex PROPERTIES WILL_FAIL TRUE)
 add_test(NAME noqeri-nqri-selftest COMMAND noqeri-nqri --selftest)
 add_test(NAME noqeri-version COMMAND noqeri --version)
 find_program(NOQERI_NODE_EXECUTABLE NAMES node node.exe)
 if(NOQERI_NODE_EXECUTABLE)
  add_test(NAME noqeri-agent-loop
   COMMAND ${CMAKE_COMMAND} -E env
   "NOQERI_TEST_COMPILER=$<TARGET_FILE:noqeri>"
   "${NOQERI_NODE_EXECUTABLE}" --test
   "${CMAKE_SOURCE_DIR}/tests/agent_loop.test.mjs")
 endif()
 add_test(NAME noqeri-native-web
  COMMAND ${CMAKE_COMMAND}
   "-DNOQERI_WEB=$<TARGET_FILE:noqeri-web>"
   "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
   "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
   -P ${CMAKE_SOURCE_DIR}/tests/cmake/native_web_smoke.cmake)
 add_test(NAME noqeri-check-hello COMMAND noqeri check ${CMAKE_SOURCE_DIR}/examples/hello.noqeri)
 add_test(NAME noqeri-tests COMMAND noqeri test ${CMAKE_SOURCE_DIR}/tests)
 add_test(NAME noqeri-noqeri-owned-components COMMAND noqeri run ${CMAKE_SOURCE_DIR}/tests/noqeri_owned_components.noqeri)
 add_test(NAME noqeri-library-suite COMMAND noqeri run ${CMAKE_SOURCE_DIR}/tests/noqeri_library_suite.noqeri)
 add_test(NAME noqeri-ecosystem-smoke COMMAND noqeri run ${CMAKE_SOURCE_DIR}/examples/ecosystem_smoke.noqeri)
 add_test(NAME noqeri-short-circuit COMMAND noqeri run ${CMAKE_SOURCE_DIR}/tests/short_circuit.noqeri)
 add_test(NAME noqeri-selftest-program COMMAND noqeri run ${CMAKE_SOURCE_DIR}/Programs/selftest.noqeri)
 add_test(NAME noqeri-systems-types COMMAND noqeri check ${CMAKE_SOURCE_DIR}/tests/systems_types.noqeri)
 add_test(NAME noqeri-systems-native COMMAND noqeri build ${CMAKE_SOURCE_DIR}/tests/systems_types.noqeri ${CMAKE_BINARY_DIR}/systems_types.s)
 add_test(NAME noqeri-general-capabilities COMMAND noqeri check ${CMAKE_SOURCE_DIR}/tests/general_capabilities.noqeri)
 add_test(NAME noqeri-module-imports COMMAND noqeri check ${CMAKE_SOURCE_DIR}/tests/modules_imports.noqeri)
 add_test(NAME noqeri-general-native COMMAND noqeri build ${CMAKE_SOURCE_DIR}/examples/general_systems.noqeri ${CMAKE_BINARY_DIR}/general_systems.s)
 add_test(NAME noqeri-freestanding-build COMMAND noqeri build ${CMAKE_SOURCE_DIR}/examples/native_hello.noqeri ${CMAKE_BINARY_DIR}/native_hello.s)
 add_test(NAME noqeri-noqeridb COMMAND noqeri db ${CMAKE_SOURCE_DIR}/tests/noqeridb.nqd ${CMAKE_BINARY_DIR}/noqeridb-test.nqdb)
 foreach(case IN ITEMS unscoped_delete unscoped_update malformed_where schema_drift)
  if(case STREQUAL "unscoped_delete")
   set(fixture noqeridb_reject_unscoped_delete.nqd)
  elseif(case STREQUAL "unscoped_update")
   set(fixture noqeridb_reject_unscoped_update.nqd)
  elseif(case STREQUAL "malformed_where")
   set(fixture noqeridb_reject_malformed_where.nqd)
  else()
   set(fixture noqeridb_reject_schema_drift.nqd)
  endif()
  add_test(NAME noqeridb-reject-${case} COMMAND noqeri db ${CMAKE_SOURCE_DIR}/tests/${fixture} ${CMAKE_BINARY_DIR}/noqeridb-reject-${case}.nqdb)
  set_tests_properties(noqeridb-reject-${case} PROPERTIES WILL_FAIL TRUE)
 endforeach()
 foreach(kind IN ITEMS bool number)
  add_test(NAME noqeridb-reject-corrupt-${kind}
   COMMAND noqeri db ${CMAKE_SOURCE_DIR}/tests/noqeridb_read.nqd
    ${CMAKE_SOURCE_DIR}/tests/noqeridb_corrupt_${kind}.nqdb)
  set_tests_properties(noqeridb-reject-corrupt-${kind} PROPERTIES WILL_FAIL TRUE)
 endforeach()
 add_test(NAME noqeri-stress COMMAND noqeri stress ${CMAKE_SOURCE_DIR})
 add_test(NAME noqeri-doctor COMMAND noqeri doctor ${CMAKE_SOURCE_DIR})
 add_test(NAME noqeri-audit COMMAND noqeri audit ${CMAKE_SOURCE_DIR})
endif()

# Experimental POSIX mailbox host runtime. This is a native C ABI adapter,
# not yet an integrated Noqeri standard library or a production SMTP server.
if(UNIX AND NOT APPLE)
 find_package(Threads REQUIRED)
 add_library(noqeri_mailbox_runtime STATIC Runtime/native/mailbox_runtime.cpp)
 target_include_directories(noqeri_mailbox_runtime PUBLIC Include)
 target_compile_features(noqeri_mailbox_runtime PUBLIC cxx_std_17)
 target_link_libraries(noqeri_mailbox_runtime PUBLIC Threads::Threads)
 noqeri_apply_warnings(noqeri_mailbox_runtime)
 if(BUILD_TESTING)
  add_executable(noqeri-mailbox-runtime-test tests/cpp/mailbox_runtime_test.cpp)
  target_link_libraries(noqeri-mailbox-runtime-test PRIVATE noqeri_mailbox_runtime)
  noqeri_apply_warnings(noqeri-mailbox-runtime-test)
  add_test(NAME noqeri-mailbox-runtime COMMAND noqeri-mailbox-runtime-test)
 endif()
endif()

# Linux-only experimental mailbox TLS/reactor/durable message helpers.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
 find_package(OpenSSL REQUIRED)
 add_library(noqeri_mailbox_extended STATIC Runtime/native/mailbox_extended.cpp)
 target_include_directories(noqeri_mailbox_extended PUBLIC Include)
 target_compile_features(noqeri_mailbox_extended PUBLIC cxx_std_17)
 target_link_libraries(noqeri_mailbox_extended PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
 noqeri_apply_warnings(noqeri_mailbox_extended)
 if(BUILD_TESTING)
  add_executable(noqeri-mailbox-extended-test tests/cpp/mailbox_extended_test.cpp)
  target_link_libraries(noqeri-mailbox-extended-test PRIVATE noqeri_mailbox_extended)
  noqeri_apply_warnings(noqeri-mailbox-extended-test)
  add_test(NAME noqeri-mailbox-extended COMMAND noqeri-mailbox-extended-test)
  add_executable(noqeri-mailbox-load-test tests/cpp/mailbox_load_test.cpp)
  target_link_libraries(noqeri-mailbox-load-test PRIVATE noqeri_mailbox_extended)
  noqeri_apply_warnings(noqeri-mailbox-load-test)
  add_test(NAME noqeri-mailbox-load COMMAND noqeri-mailbox-load-test 2000)
  set_tests_properties(noqeri-mailbox-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_executable(noqeri-mailbox-tls-test tests/cpp/mailbox_tls_test.cpp)
  target_link_libraries(noqeri-mailbox-tls-test PRIVATE noqeri_mailbox_extended)
  noqeri_apply_warnings(noqeri-mailbox-tls-test)
  add_test(NAME noqeri-mailbox-tls COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run-mailbox-tls.sh $<TARGET_FILE:noqeri-mailbox-tls-test>)
 endif()
endif()
install(TARGETS noqeri noqeri-nqri noqeri-web noqeri_abi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# Header installed through existing public include/noqeri rule.
install(DIRECTORY Include/noqeri/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/noqeri FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY Lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Lib FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Modules/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Modules FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Objects/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Objects FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Database/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Database FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Runtime/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Runtime FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Parser/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/Parser FILES_MATCHING PATTERN "*.noqeri")
install(DIRECTORY Platforms/ PC/ Mac/ Android/ DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/platforms FILES_MATCHING PATTERN "*.noqeri")
install(FILES Brand/noqeri-logo.webp Brand/noqeri-mark.png DESTINATION ${CMAKE_INSTALL_DATADIR}/noqeri/brand)

# Experimental PGAS transport. Kept separate from the compiler and mailbox
# runtime until native Noqeri FFI linking and security qualification pass.
if(UNIX AND NOT APPLE)
 find_package(Threads REQUIRED)
 find_package(OpenSSL REQUIRED)
 # RDMA is an OPTIONAL real libibverbs backend. Without rdma-core headers
 # and shared library, the API exists but explicitly returns -ENOTSUP.
 option(NOQERI_ENABLE_RDMA "Build optional libibverbs RDMA provider" ON)
 add_library(noqeri_pgas_rdma STATIC Runtime/native/pgas_rdma.cpp)
 target_include_directories(noqeri_pgas_rdma PUBLIC Include)
 target_compile_features(noqeri_pgas_rdma PUBLIC cxx_std_17)
 if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOQERI_ENABLE_RDMA)
  find_path(NOQERI_IBVERBS_INCLUDE_DIR infiniband/verbs.h)
  find_library(NOQERI_IBVERBS_LIBRARY ibverbs)
  if(NOQERI_IBVERBS_INCLUDE_DIR AND NOQERI_IBVERBS_LIBRARY)
   target_compile_definitions(noqeri_pgas_rdma PRIVATE NOQERI_HAS_IBVERBS=1)
   target_include_directories(noqeri_pgas_rdma PRIVATE ${NOQERI_IBVERBS_INCLUDE_DIR})
   target_link_libraries(noqeri_pgas_rdma PUBLIC ${NOQERI_IBVERBS_LIBRARY})
   message(STATUS "Noqeri experimental RDMA: libibverbs provider enabled")
  else()
   message(STATUS "Noqeri RDMA: libibverbs unavailable, explicit ENOTSUP stub only")
  endif()
 endif()
 noqeri_apply_warnings(noqeri_pgas_rdma)
 add_library(noqeri_pgas_runtime STATIC Runtime/native/pgas_runtime.cpp)
 add_library(noqeri_pgas_replication STATIC Runtime/native/pgas_replication.cpp)
 target_include_directories(noqeri_pgas_replication PUBLIC Include)
 target_compile_features(noqeri_pgas_replication PUBLIC cxx_std_17)
 target_link_libraries(noqeri_pgas_replication PUBLIC noqeri_pgas_tls_gateway)
 noqeri_apply_warnings(noqeri_pgas_replication)
 add_library(noqeri_pgas_tls_gateway STATIC Runtime/native/pgas_tls_gateway.cpp)
 target_include_directories(noqeri_pgas_tls_gateway PUBLIC Include)
 target_compile_features(noqeri_pgas_tls_gateway PUBLIC cxx_std_17)
 target_link_libraries(noqeri_pgas_tls_gateway PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
 noqeri_apply_warnings(noqeri_pgas_tls_gateway)
 target_include_directories(noqeri_pgas_runtime PUBLIC Include)
 target_compile_features(noqeri_pgas_runtime PUBLIC cxx_std_17)
 target_link_libraries(noqeri_pgas_runtime PUBLIC Threads::Threads)
 noqeri_apply_warnings(noqeri_pgas_runtime)
 if(BUILD_TESTING)
  add_executable(noqeri-pgas-runtime-test tests/cpp/pgas_runtime_test.cpp)
  target_link_libraries(noqeri-pgas-runtime-test PRIVATE noqeri_pgas_runtime)
  noqeri_apply_warnings(noqeri-pgas-runtime-test)
  add_test(NAME noqeri-pgas-runtime COMMAND noqeri-pgas-runtime-test)
  add_executable(noqeri-pgas-process-test tests/cpp/pgas_process_test.cpp)
  target_link_libraries(noqeri-pgas-process-test PRIVATE noqeri_pgas_runtime)
  noqeri_apply_warnings(noqeri-pgas-process-test)
  add_test(NAME noqeri-pgas-process COMMAND noqeri-pgas-process-test)
  add_executable(noqeri-pgas-durable-test tests/cpp/pgas_durable_test.cpp)
  target_link_libraries(noqeri-pgas-durable-test PRIVATE noqeri_pgas_runtime)
  noqeri_apply_warnings(noqeri-pgas-durable-test)
  add_test(NAME noqeri-pgas-durable COMMAND noqeri-pgas-durable-test)
  add_executable(noqeri-pgas-tls-test tests/cpp/pgas_tls_test.cpp)
  target_link_libraries(noqeri-pgas-tls-test PRIVATE noqeri_pgas_runtime noqeri_pgas_tls_gateway)
  noqeri_apply_warnings(noqeri-pgas-tls-test)
  add_test(NAME noqeri-pgas-tls COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/pgas-tls-cert-test.sh $<TARGET_FILE:noqeri-pgas-tls-test>)
  add_executable(noqeri-pgas-rdma-probe-test tests/cpp/pgas_rdma_probe_test.cpp)
  target_link_libraries(noqeri-pgas-rdma-probe-test PRIVATE noqeri_pgas_rdma)
  noqeri_apply_warnings(noqeri-pgas-rdma-probe-test)
  add_test(NAME noqeri-pgas-rdma-probe COMMAND noqeri-pgas-rdma-probe-test)
  add_executable(noqeri-pgas-replication-test tests/cpp/pgas_replication_test.cpp)
  target_link_libraries(noqeri-pgas-replication-test PRIVATE noqeri_pgas_runtime noqeri_pgas_replication)
  noqeri_apply_warnings(noqeri-pgas-replication-test)
  add_test(NAME noqeri-pgas-replication
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/pgas-tls-cert-test.sh $<TARGET_FILE:noqeri-pgas-replication-test>)
  # Operator-run two-physical-host mTLS acceptance tool: compiled in CI but
  # not misreported as physically tested by a single GitHub runner.
  add_executable(noqeri-pgas-cross-host-smoke tests/cpp/pgas_cross_host_smoke.cpp)
  target_link_libraries(noqeri-pgas-cross-host-smoke PRIVATE noqeri_pgas_runtime noqeri_pgas_tls_gateway)
  noqeri_apply_warnings(noqeri-pgas-cross-host-smoke)
 endif()
endif()


# Full builds use the same native packaging metadata as the portable SDK.
include(cmake/NoqeriPackaging.cmake)
