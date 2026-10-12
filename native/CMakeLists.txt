# Web98 native C++/CEF browser.
#
# Configure (VS2022 x64, sandbox bootstrap mode):
#   cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64 ^
#         -DCEF_ROOT=D:/path/to/cef_binary_...
# Build:
#   cmake --build native/build --config Release --parallel
# Output:
#   native/build/Release/web98.exe (+ web98.dll, CEF runtime, res/)
#
# CEF_ROOT may also be provided via the CEF_ROOT environment variable.
cmake_minimum_required(VERSION 3.21)
project(web98 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate the CEF binary distribution.
if(NOT CEF_ROOT AND DEFINED ENV{CEF_ROOT})
  set(CEF_ROOT "$ENV{CEF_ROOT}")
endif()
if(NOT CEF_ROOT)
  set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_cef/cef" CACHE PATH "CEF binary distribution root")
endif()
if(NOT IS_DIRECTORY "${CEF_ROOT}/cmake")
  message(FATAL_ERROR
    "CEF distribution not found at '${CEF_ROOT}'. Download a windows64 'standard' "
    "build from https://cef-builds.spotifycdn.com/index.html, extract it and pass "
    "-DCEF_ROOT=<dir> (or set the CEF_ROOT environment variable).")
endif()
file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# CEF's C++ wrapper library.
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

# Imported libcef target.
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

# ---------------------------------------------------------------------------
# sqlite3 (bundled amalgamation, own flags: CEF's /W4 /WX breaks on sqlite)
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
target_include_directories(sqlite3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_OMIT_LOAD_EXTENSION=1
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_DEPRECATED=1
  SQLITE_THREADSAFE=1
  )

# ---------------------------------------------------------------------------
# Core logic shared between the app and unit tests (no CEF includes).
# ---------------------------------------------------------------------------
set(WEB98_CORE_SRCS
  src/json.cpp
  src/json.h
  src/safeurl.cpp
  src/safeurl.h
  src/urlbar.cpp
  src/urlbar.h
  src/settings.cpp
  src/settings.h
  src/storage.cpp
  src/storage.h
  src/dlpath.cpp
  src/dlpath.h
  src/security.cpp
  src/security.h
  src/base.cpp
  src/base.h
  )

# ---------------------------------------------------------------------------
# web98 browser target.
# With USE_SANDBOX (default) the app is a client DLL loaded by bootstrap.exe,
# which cmake copies to web98.exe — sandboxed subprocess launch on Windows.
# ---------------------------------------------------------------------------
set(CEF_TARGET "web98")
set(WEB98_SRCS
  ${WEB98_CORE_SRCS}
  src/win_main.cpp
  src/app_core.cpp
  src/app_core.h
  src/window.cpp
  src/window.h
  src/dialogs.cpp
  src/dialogs.h
  src/menus.cpp
  src/menus.h
  src/cef_app.cpp
  src/cef_app.h
  src/cef_client.cpp
  src/cef_client.h
  src/cef_engine.cpp
  src/cef_engine.h
  src/cef_util.h
  src/engine.h
  src/downloads.cpp
  src/downloads.h
  src/session.cpp
  src/session.h
  )

if(USE_SANDBOX)
  add_library(${CEF_TARGET} SHARED ${WEB98_SRCS})
  SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})
  COPY_SINGLE_FILE(${CEF_TARGET} ${CEF_BINARY_DIR}/bootstrap.exe ${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe)
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(${CEF_TARGET} PROPERTIES
      VS_DEBUGGER_COMMAND "$<TARGET_FILE_DIR:${CEF_TARGET}>/${CEF_TARGET}.exe"
      )
  endif()
else()
  add_executable(${CEF_TARGET} WIN32 ${WEB98_SRCS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  ADD_WINDOWS_MANIFEST("${CMAKE_CURRENT_SOURCE_DIR}/win" "${CEF_TARGET}" "exe")
endif()

target_include_directories(${CEF_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(${CEF_TARGET} libcef_dll_wrapper sqlite3)
# CEF_STANDARD_LIBS lacks shell32/ole32 (SHGetKnownFolderPath in base.cpp)
# and psapi (GetProcessMemoryInfo in app_core.cpp).
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper sqlite3
  shell32 ole32 psapi gdiplus ${CEF_STANDARD_LIBS})

# Local pages (renderer) + assets, mirroring the repo layout so the
# ../../assets and ../../css references inside the pages keep working:
#   res/src/renderer/{index,startpage,error}.html + dialogs/
#   res/assets/...  res/css/...
set(WEB98_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${WEB98_REPO_ROOT}/src/renderer" "${CEF_TARGET_OUT_DIR}/res/src/renderer"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${WEB98_REPO_ROOT}/assets" "${CEF_TARGET_OUT_DIR}/res/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${WEB98_REPO_ROOT}/css" "${CEF_TARGET_OUT_DIR}/res/css"
  VERBATIM
  )
# CEF runtime + resources.
COPY_FILES(${CEF_TARGET} "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(${CEF_TARGET} "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

if(USE_SANDBOX)
  SET_LPAC_ACLS("${CEF_TARGET}")
endif()

# ---------------------------------------------------------------------------
# Unit tests (pure logic: json/safeurl/urlbar/settings/storage/security/
# dlpath). No CEF linkage required.
# ---------------------------------------------------------------------------
set(WEB98_UNIT_SRCS
  ${WEB98_CORE_SRCS}
  tests/unit_tests.cpp
  )
add_executable(web98_unit ${WEB98_UNIT_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(web98_unit)
target_include_directories(web98_unit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# base.cpp uses SHGetKnownFolderPath (shell32/ole32) and shlwapi helpers.
target_link_libraries(web98_unit sqlite3 shell32 ole32 shlwapi uuid)
set_target_properties(web98_unit PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:web98_unit>"
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CEF_TARGET_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CEF_TARGET_OUT_DIR}"
  )

PRINT_CEF_CONFIG()
