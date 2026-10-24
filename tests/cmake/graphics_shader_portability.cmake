# Cross-OS shader compiler smoke: use only CMake, no JavaScript, shell, Naga
# binary, real GPU, or host graphics driver. The Noqeri shader command itself
# must build and emit reproducible WGSL on the target OS.
if(NOT DEFINED NOQERI_COMPILER OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_ROOT)
 message(FATAL_ERROR "Noqeri shader portability test requires compiler, source and output")
endif()
set(out_dir "${OUTPUT_ROOT}/noqeri-cross-platform-wgsl")
file(MAKE_DIRECTORY "${out_dir}")
foreach(fixture IN ITEMS "color_gradient;fragment;@fragment"
                         "invert_compute;compute;@compute"
                         "vertex3d;vertex;@vertex"
                         "vector_lighting_fragment;fragment;cross(")
 # Delimit manually because cmake loops semicolon-separated values.
 string(REPLACE ";" "|" joined "${fixture}")
 string(REPLACE "|" ";" fields "${joined}")
 list(GET fields 0 name)
 list(GET fields 1 stage)
 list(GET fields 2 expected)
 set(input "${SOURCE_ROOT}/examples/shaders/${name}.noqeri")
 set(output "${out_dir}/${name}.wgsl")
 execute_process(
  COMMAND "${NOQERI_COMPILER}" shader "${input}" "${output}" "--stage=${stage}"
  RESULT_VARIABLE status ERROR_VARIABLE diagnostics OUTPUT_VARIABLE info)
 if(NOT status EQUAL 0)
  message(FATAL_ERROR "${name} shader compiler failed: ${diagnostics} ${info}")
 endif()
 file(READ "${output}" first)
 string(FIND "${first}" "${expected}" index)
 if(index EQUAL -1)
  message(FATAL_ERROR "${name} shader missing expected WGSL stage/intrinsic")
 endif()
 if(NOT first MATCHES "fn noqeri_shade\\(")
  message(FATAL_ERROR "${name} shader missing typed Noqeri function")
 endif()
 execute_process(
  COMMAND "${NOQERI_COMPILER}" shader "${input}" "${output}" "--stage=${stage}"
  RESULT_VARIABLE status ERROR_VARIABLE diagnostics)
 if(NOT status EQUAL 0)
  message(FATAL_ERROR "${name} repeated compilation failed: ${diagnostics}")
 endif()
 file(READ "${output}" second)
 if(NOT first STREQUAL second)
  message(FATAL_ERROR "${name} shader generation nondeterministic")
 endif()
 message(STATUS "Noqeri portable WGSL: PASS ${name} (${stage})")
endforeach()
set(bad "${out_dir}/reject.noqeri")
set(output "${out_dir}/retain.wgsl")
file(WRITE "${bad}" "export function shade(value: vec4): vec4 { return sampleTexture(value.xy) }\n")
file(WRITE "${output}" "retain-last-known-good")
execute_process(
 COMMAND "${NOQERI_COMPILER}" shader "${bad}" "${output}" "--stage=compute"
 RESULT_VARIABLE status ERROR_VARIABLE diagnostics)
if(status EQUAL 0)
 message(FATAL_ERROR "Invalid resource access in compute shader accepted")
endif()
file(READ "${output}" last_good)
if(NOT last_good STREQUAL "retain-last-known-good")
 message(FATAL_ERROR "Failed shader compilation overwrote valid output")
endif()
message(STATUS "Noqeri portable shader compiler stages, determinism, fail-closed: PASS")
