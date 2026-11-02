# CTest smoke suite for the native compiler only. No Node.js/JS required.
if(NOT DEFINED NOQERI_WEB OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_ROOT)
  message(FATAL_ERROR "missing CTest native web arguments")
endif()
set(dest "${OUTPUT_ROOT}/native-web-static")
file(REMOVE_RECURSE "${dest}")
execute_process(COMMAND "${NOQERI_WEB}" build "${SOURCE_ROOT}/examples/native_web_static" "${dest}"
 RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE errors)
if(NOT code EQUAL 0)
 message(FATAL_ERROR "native build failed: ${code}: ${log} ${errors}")
endif()
foreach(path IN ITEMS index.html about/index.html 404.html theme.css)
 if(NOT EXISTS "${dest}/${path}")
  message(FATAL_ERROR "expected generated ${path}")
 endif()
endforeach()
file(READ "${dest}/index.html" home)
file(READ "${dest}/about/index.html" about)
file(READ "${dest}/theme.css" css)
if(NOT home MATCHES "Build websites with Noqeri" OR NOT home MATCHES "about" OR NOT about MATCHES "Independent web pages")
 message(FATAL_ERROR "native page contents incorrect")
endif()
if(NOT css MATCHES "#304ac0" OR NOT css MATCHES "--nq-radius:14px")
 message(FATAL_ERROR "native theme compilation failed")
endif()
if(home MATCHES "<script" OR home MATCHES "javascript:" OR home MATCHES "[.]mjs" OR about MATCHES "<script")
 message(FATAL_ERROR "native output requires JavaScript")
endif()

set(unsafe "${OUTPUT_ROOT}/native-web-unsafe")
file(MAKE_DIRECTORY "${unsafe}")
file(WRITE "${unsafe}/main.noqeri" "let count = 0\nroute \"/\" { button on.click { count = count + 1 } { \"bad\" } }\n")
execute_process(COMMAND "${NOQERI_WEB}" build "${unsafe}" "${OUTPUT_ROOT}/native-web-rejected"
 RESULT_VARIABLE invalid_result ERROR_VARIABLE invalid_err)
if(invalid_result EQUAL 0 OR NOT invalid_err MATCHES "events require serve-app")
 message(FATAL_ERROR "No-JS mode must reject event handlers: ${invalid_err}")
endif()

file(WRITE "${unsafe}/main.noqeri" "route \"/\" { a href: \"javascript:alert(1)\" { \"bad\" } }\n")
execute_process(COMMAND "${NOQERI_WEB}" build "${unsafe}" "${OUTPUT_ROOT}/native-web-rejected"
 RESULT_VARIABLE invalid_result ERROR_VARIABLE invalid_err)
if(invalid_result EQUAL 0 OR NOT invalid_err MATCHES "unsafe URL")
 message(FATAL_ERROR "No-JS mode must reject unsafe URLs: ${invalid_err}")
endif()

file(WRITE "${unsafe}/main.noqeri" "route \"/\" { p { \"<script>alert(1)</script>\" } }\n")
execute_process(COMMAND "${NOQERI_WEB}" build "${unsafe}" "${OUTPUT_ROOT}/native-web-escaped"
 RESULT_VARIABLE escaped_code ERROR_VARIABLE escaped_errors)
if(NOT escaped_code EQUAL 0)
 message(FATAL_ERROR "native output rejected legitimate text: ${escaped_errors}")
endif()
file(READ "${OUTPUT_ROOT}/native-web-escaped/index.html" escaped_html)
if(NOT escaped_html MATCHES "&lt;script&gt;alert" OR escaped_html MATCHES "<script>")
 message(FATAL_ERROR "native HTML escaping failed")
endif()

# The same .noqeri file must supply frontend markup and multiline CSS.
# Existing project-directory builds above remain backward-compatible.
set(single_file "${SOURCE_ROOT}/examples/native_web_single_file_static.noqeri")
set(single_dest "${OUTPUT_ROOT}/native-web-single-file")
file(REMOVE_RECURSE "${single_dest}")
execute_process(COMMAND "${NOQERI_WEB}" build "${single_file}" "${single_dest}"
 RESULT_VARIABLE single_result OUTPUT_VARIABLE single_log ERROR_VARIABLE single_errors)
if(NOT single_result EQUAL 0)
 message(FATAL_ERROR "single-file web build failed: ${single_errors}")
endif()
file(READ "${single_dest}/index.html" single_html)
file(READ "${single_dest}/theme.css" single_css)
if(NOT single_html MATCHES "One file Noqeri site"
   OR NOT single_css MATCHES "[.]poster"
   OR NOT single_css MATCHES "@media"
   OR NOT single_css MATCHES "#fb5607")
 message(FATAL_ERROR "single-file frontend or CSS content missing")
endif()

# Fail closed on malformed multiline CSS and duplicate declarations.
file(WRITE "${unsafe}/main.noqeri" [=[
style """
.poster { color: red }
route "/" { p { "incomplete" } }
]=])
execute_process(COMMAND "${NOQERI_WEB}" build "${unsafe}" "${OUTPUT_ROOT}/native-web-invalid-style"
 RESULT_VARIABLE broken_style ERROR_VARIABLE broken_style_errors)
if(broken_style EQUAL 0 OR NOT broken_style_errors MATCHES "unterminated multiline")
 message(FATAL_ERROR "unterminated multiline style must fail: ${broken_style_errors}")
endif()
file(WRITE "${unsafe}/main.noqeri" [=[
style """.a { color: red }"""
style """.b { color: blue }"""
route "/" { h1 { "duplicate" } }
]=])
execute_process(COMMAND "${NOQERI_WEB}" build "${unsafe}" "${OUTPUT_ROOT}/native-web-invalid-style"
 RESULT_VARIABLE duplicate_style ERROR_VARIABLE duplicate_style_errors)
if(duplicate_style EQUAL 0 OR NOT duplicate_style_errors MATCHES "duplicate style declaration")
 message(FATAL_ERROR "duplicate inline style must fail: ${duplicate_style_errors}")
endif()

message(STATUS "Native web compiler, no-JS assets, tokens and unsafe-source regression checks passed")
