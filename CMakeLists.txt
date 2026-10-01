cmake_minimum_required(VERSION 3.24)
project(cx LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "CX P02 currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(cx WIN32
  src/main.cpp
  src/app_window.cpp
  src/app_window.h)

target_include_directories(cx PRIVATE
  "${WEBVIEW2_ROOT}/build/native/include")

target_link_libraries(cx PRIVATE
  "${WEBVIEW2_ROOT}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
  user32 ole32 shell32 shlwapi version)
target_compile_definitions(cx PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(cx PRIVATE /W4 /permissive- /EHsc)
endif()
