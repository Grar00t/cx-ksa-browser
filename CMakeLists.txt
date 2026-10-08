cmake_minimum_required(VERSION 3.24)
project(cx LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "CX currently targets Windows only.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

set(WEBVIEW2_VERSION "1.0.4258.31" CACHE STRING "Pinned Microsoft.Web.WebView2 NuGet version")
set(WEBVIEW2_ROOT "${CMAKE_BINARY_DIR}/packages/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")
set(WEBVIEW2_HEADER "${WEBVIEW2_ROOT}/build/native/include/WebView2.h")

if(NOT EXISTS "${WEBVIEW2_HEADER}")
  set(_wv2_pkg "${CMAKE_BINARY_DIR}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}.nupkg")
  string(TOLOWER "${WEBVIEW2_VERSION}" _wv2_version_lower)
  set(_wv2_url "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${_wv2_version_lower}/microsoft.web.webview2.${_wv2_version_lower}.nupkg")
  message(STATUS "Fetching pinned WebView2 SDK ${WEBVIEW2_VERSION} from NuGet")
  file(DOWNLOAD "${_wv2_url}" "${_wv2_pkg}" TLS_VERIFY ON STATUS _wv2_status)
  list(GET _wv2_status 0 _wv2_code)
  if(NOT _wv2_code EQUAL 0)
    message(FATAL_ERROR "WebView2 NuGet download failed: ${_wv2_status}")
  endif()
  file(MAKE_DIRECTORY "${WEBVIEW2_ROOT}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xvf "${_wv2_pkg}"
    WORKING_DIRECTORY "${WEBVIEW2_ROOT}"
    RESULT_VARIABLE _wv2_extract)
  if(NOT _wv2_extract EQUAL 0)
    message(FATAL_ERROR "WebView2 NuGet extraction failed")
  endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WEBVIEW2_ARCH x64)
else()
  set(WEBVIEW2_ARCH x86)
endif()

add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /W3)
endif()
add_library(cx_storage STATIC
  src/storage/database.cpp
  src/storage/database.h
  src/storage/migrations.h)
target_include_directories(cx_storage PUBLIC src)
target_link_libraries(cx_storage PUBLIC sqlite3)
if(MSVC)
  target_compile_options(cx_storage PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_localization STATIC
  src/localization/strings.cpp
  src/localization/strings.h)
target_include_directories(cx_localization PUBLIC src)
target_compile_definitions(cx_localization PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(cx_localization PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_config STATIC
  src/config/config_manager.cpp
  src/config/config_manager.h
  src/config/settings_schema.h
  src/config/backup_restore.cpp
  src/config/backup_restore.h)
target_include_directories(cx_config PUBLIC src)
target_compile_definitions(cx_config PRIVATE UNICODE _UNICODE)
target_link_libraries(cx_config PUBLIC cx_storage)
if(MSVC)
  target_compile_options(cx_config PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_browser STATIC
  src/browser/tab_manager.cpp
  src/browser/tab_manager.h
  src/browser/navigation_controller.cpp
  src/browser/navigation_controller.h
  src/browser/history_service.cpp
  src/browser/history_service.h
  src/browser/bookmark_service.cpp
  src/browser/bookmark_service.h
  src/browser/history_dialog.cpp
  src/browser/history_dialog.h
  src/browser/bookmarks_dialog.cpp
  src/browser/bookmarks_dialog.h)
target_include_directories(cx_browser PUBLIC src)
target_compile_definitions(cx_browser PRIVATE UNICODE _UNICODE)
target_link_libraries(cx_browser PUBLIC cx_storage user32)
if(MSVC)
  target_compile_options(cx_browser PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_agent STATIC
  src/agent/agent_core.cpp
  src/agent/agent_core.h
  src/agent/permissions.cpp
  src/agent/permissions.h
  src/agent/consent_dialog.cpp
  src/agent/consent_dialog.h)
target_include_directories(cx_agent PUBLIC src)
target_link_libraries(cx_agent PUBLIC cx_storage user32)
if(MSVC)
  target_compile_options(cx_agent PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_mcp STATIC
  src/mcp/mcp_client.cpp
  src/mcp/mcp_client.h
  src/mcp/allowlist_manager.cpp
  src/mcp/allowlist_manager.h
  src/mcp/rate_limiter.cpp
  src/mcp/rate_limiter.h
  src/mcp/allowlist_dialog.cpp
  src/mcp/allowlist_dialog.h)
target_include_directories(cx_mcp PUBLIC src)
target_compile_definitions(cx_mcp PRIVATE UNICODE _UNICODE)
target_link_libraries(cx_mcp PUBLIC cx_agent user32 comdlg32)
if(MSVC)
  target_compile_options(cx_mcp PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(cx_ui STATIC
  src/ui/agent_workspace.cpp
  src/ui/agent_workspace.h
  src/ui/athar_sound.cpp
  src/ui/athar_sound.h
  src/ui/context_graph.cpp
  src/ui/context_graph.h
  src/ui/design_tokens.h
  src/ui/najdi_theme.cpp
  src/ui/najdi_theme.h
  src/ui/settings_window.cpp
  src/ui/settings_window.h
  src/ui/privacy_dashboard.cpp
  src/ui/privacy_dashboard.h
  src/ui/permission_dialog.cpp
  src/ui/permission_dialog.h)
target_include_directories(cx_ui PUBLIC src)
target_compile_definitions(cx_ui PRIVATE UNICODE _UNICODE)
target_link_libraries(cx_ui PUBLIC
  cx_localization
  cx_mcp
  cx_browser
  user32
  shell32
  comctl32
  winmm)
if(MSVC)
  target_compile_options(cx_ui PRIVATE /W4 /permissive- /EHsc)
endif()

add_executable(cx WIN32
  src/main.cpp
  src/app_window.cpp
  src/app_window.h)
target_include_directories(cx PRIVATE
  "${WEBVIEW2_ROOT}/build/native/include")
target_link_libraries(cx PRIVATE
  cx_ui
  "${WEBVIEW2_ROOT}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
  user32 ole32 shell32 shlwapi version comctl32)
target_compile_definitions(cx PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(cx PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(gtest STATIC
  third_party/googletest/src/gtest-all.cc)
target_include_directories(gtest
  PUBLIC third_party/googletest/include
  PRIVATE third_party/googletest)
if(MSVC)
  target_compile_options(gtest PRIVATE /W3)
endif()

add_library(gtest_main STATIC
  third_party/googletest/src/gtest_main.cc)
target_link_libraries(gtest_main PUBLIC gtest)
add_library(GTest::gtest ALIAS gtest)
add_library(GTest::gtest_main ALIAS gtest_main)

enable_testing()

add_executable(mcp_test_server
  tests/mcp_test_server.cpp)
set_target_properties(mcp_test_server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests-bin")

add_executable(browser_crash_writer
  tests/browser_crash_writer.cpp)
target_link_libraries(browser_crash_writer PRIVATE cx_browser)
set_target_properties(browser_crash_writer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests-bin")

add_executable(storage_tests
  tests/storage_test.cpp
  tests/agent_test.cpp
  tests/mcp_test.cpp
  tests/browser_test.cpp
  tests/ui_test.cpp
  tests/config_test.cpp
  tests/integration_test.cpp
  tests/security_test.cpp
  tests/performance_test.cpp)
target_link_libraries(storage_tests PRIVATE
  cx_ui
  cx_config
  GTest::gtest_main
  iphlpapi)
add_dependencies(storage_tests mcp_test_server browser_crash_writer)
if(MSVC)
  target_compile_options(storage_tests PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(storage_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests-bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests-bin")

add_custom_command(TARGET storage_tests POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DTEST_FILE=${CMAKE_BINARY_DIR}/CTestTestfile.cmake"
    "-DTEST_EXE=$<TARGET_FILE:storage_tests>"
    -P "${CMAKE_SOURCE_DIR}/cmake/write_test_manifest.cmake"
  VERBATIM)
