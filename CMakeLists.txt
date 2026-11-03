cmake_minimum_required(VERSION 3.24)
project(FT710Dashboard VERSION 2.0.5 LANGUAGES C CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 20)

option(BUILD_DEBUG "Enable debug logging output (e.g., RSS memory, JSON build progress)" OFF)
if(BUILD_DEBUG)
  add_definitions(-DBUILD_DEBUG)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  message(STATUS "Configuring FT-710 Dashboard for macOS (arm64 / x86_64)")

  execute_process(COMMAND brew --prefix hamlib OUTPUT_VARIABLE HAMLIB_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND brew --prefix portaudio OUTPUT_VARIABLE PORTAUDIO_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND brew --prefix openssl@3 OUTPUT_VARIABLE OPENSSL_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)

  find_path(HAMLIB_INCLUDE_DIR hamlib/rig.h HINTS "${HAMLIB_PREFIX}/include" REQUIRED)
  find_library(HAMLIB_LIBRARY NAMES hamlib HINTS "${HAMLIB_PREFIX}/lib" REQUIRED)

  find_path(PORTAUDIO_INCLUDE_DIR portaudio.h HINTS "${PORTAUDIO_PREFIX}/include" REQUIRED)
  find_library(PORTAUDIO_LIBRARY NAMES portaudio HINTS "${PORTAUDIO_PREFIX}/lib" REQUIRED)

  find_path(OPENSSL_INCLUDE_DIR openssl/ssl.h HINTS "${OPENSSL_PREFIX}/include" REQUIRED)
  find_library(OPENSSL_SSL_LIBRARY NAMES ssl HINTS "${OPENSSL_PREFIX}/lib" REQUIRED)
  find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto HINTS "${OPENSSL_PREFIX}/lib" REQUIRED)

  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

  set(MAC_SOURCES
    src/main_mac.cpp
    src/controller_mac.cpp
    src/radio.cpp
    src/rigctl_protocol.cpp
    src/cat_server.cpp
    src/audio_mac.cpp
    src/audio_scope.cpp
    src/browser_audio.cpp
    src/rf_scope_mac.cpp
    src/web_tls_openssl.cpp
    src/web_server.cpp
    src/web_resources.cpp
  )

  add_executable(FT710Dashboard-macOS ${MAC_SOURCES})
  target_include_directories(FT710Dashboard-macOS PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${HAMLIB_INCLUDE_DIR}
    ${PORTAUDIO_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
  )
  target_link_libraries(FT710Dashboard-macOS PRIVATE
    ${HAMLIB_LIBRARY}
    ${PORTAUDIO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${COREAUDIO_FRAMEWORK}
    ${AUDIOTOOLBOX_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
  )
  target_compile_options(FT710Dashboard-macOS PRIVATE -Wall -Wextra)

  # Copy web assets to target directory
  add_custom_command(TARGET FT710Dashboard-macOS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/web $<TARGET_FILE_DIR:FT710Dashboard-macOS>/web
  )

elseif(WIN32)
  enable_language(RC)

  set(HAMLIB_ROOT "${CMAKE_SOURCE_DIR}/vendor/hamlib-w64-4.7.2" CACHE PATH "Hamlib Windows SDK directory")
  find_path(HAMLIB_INCLUDE_DIR hamlib/rig.h HINTS "${HAMLIB_ROOT}/include" REQUIRED NO_DEFAULT_PATH)
  find_library(HAMLIB_LIBRARY NAMES hamlib libhamlib.dll.a HINTS "${HAMLIB_ROOT}/lib/gcc" REQUIRED NO_DEFAULT_PATH)

  set(PORTAUDIO_ROOT "${CMAKE_SOURCE_DIR}/vendor/portaudio-19.7.0" CACHE PATH "Pinned PortAudio source directory")
  if(NOT EXISTS "${PORTAUDIO_ROOT}/include/portaudio.h")
    message(FATAL_ERROR "PortAudio source is missing. Run scripts/build.ps1 to obtain the pinned dependency.")
  endif()
  function(configure_audio_driver)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(PA_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
    set(PA_USE_DS OFF CACHE BOOL "" FORCE)
    set(PA_USE_WMME OFF CACHE BOOL "" FORCE)
    set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PORTAUDIO_ROOT}" "${CMAKE_BINARY_DIR}/portaudio" EXCLUDE_FROM_ALL)
    get_target_property(audio_sources portaudio_static SOURCES)
    list(FILTER audio_sources EXCLUDE REGEX "(^|/)pa_win_wdmks\\.c$")
    set_property(TARGET portaudio_static PROPERTY SOURCES ${audio_sources})
    target_sources(portaudio_static PRIVATE "${CMAKE_SOURCE_DIR}/src/portaudio_wdmks.c")
    target_include_directories(portaudio_static PRIVATE "${PORTAUDIO_ROOT}/src/hostapi/wdmks")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/portaudio_wdmks.c" PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE")
  endfunction()
  configure_audio_driver()

  configure_file(src/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
  set(WEB_ASSETS "${CMAKE_SOURCE_DIR}/web/index.html" "${CMAKE_SOURCE_DIR}/web/app.css" "${CMAKE_SOURCE_DIR}/web/app.js" "${CMAKE_SOURCE_DIR}/web/control-core.js" "${CMAKE_SOURCE_DIR}/web/audio-worklet.js" "${CMAKE_SOURCE_DIR}/web/audio-playback.js")
  set_source_files_properties("${CMAKE_BINARY_DIR}/app.rc" PROPERTIES OBJECT_DEPENDS "${WEB_ASSETS}")
  set(DASHBOARD_SOURCES src/radio.cpp src/rigctl_protocol.cpp src/cat_server.cpp src/audio.cpp src/audio_direct.cpp src/audio_scope.cpp src/scope_view.cpp src/browser_audio.cpp src/web_tls.cpp src/web_server.cpp src/dashboard.cpp src/dashboard_web.cpp)
  list(APPEND DASHBOARD_SOURCES src/rf_scope.cpp)
  add_executable(FT710Dashboard WIN32 src/main.cpp ${DASHBOARD_SOURCES} "${CMAKE_BINARY_DIR}/app.rc")
  target_include_directories(FT710Dashboard PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src>" "$<$<COMPILE_LANGUAGE:CXX>:${HAMLIB_INCLUDE_DIR}>" "$<$<COMPILE_LANGUAGE:CXX>:${PORTAUDIO_ROOT}/include>")
  target_compile_definitions(FT710Dashboard PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_libraries(FT710Dashboard PRIVATE "${HAMLIB_LIBRARY}" "$<LINK_ONLY:portaudio_static>" ws2_32 comctl32 dwmapi gdiplus comdlg32 shell32 advapi32 uxtheme ole32 uuid avrt wtsapi32 secur32 crypt32 bcrypt iphlpapi)
  if(MINGW)
    target_compile_options(FT710Dashboard PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(FT710Dashboard PRIVATE -municode -static-libgcc -static-libstdc++)
  endif()
  file(GLOB HAMLIB_DLLS "${HAMLIB_ROOT}/bin/*.dll")
  if(MINGW)
    get_filename_component(COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(RUNTIME_NAME libwinpthread-1.dll libgcc_s_seh-1.dll)
      find_file(COMPILER_${RUNTIME_NAME} NAMES "${RUNTIME_NAME}" HINTS "${COMPILER_BIN}" REQUIRED NO_DEFAULT_PATH)
      list(FILTER HAMLIB_DLLS EXCLUDE REGEX "/${RUNTIME_NAME}$")
      list(APPEND HAMLIB_DLLS "${COMPILER_${RUNTIME_NAME}}")
    endforeach()
  endif()
  foreach(DLL IN LISTS HAMLIB_DLLS)
    add_custom_command(TARGET FT710Dashboard POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:FT710Dashboard>")
  endforeach()
  set(FTDI_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/runtime/ftdi" CACHE PATH "Directory containing the FTDI x64 runtime DLLs")
  set(FTDI_RUNTIME_DLLS)
  foreach(FTDI_DLL IN ITEMS FTD2XX.dll LibFT4222.dll)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DIR}/${FTDI_DLL}")
    if(NOT EXISTS "${FTDI_RUNTIME_DLL}")
      message(STATUS "Optional ${FTDI_DLL} not staged; obtain it from FTDI to use RF capture at runtime.")
      continue()
    endif()
    list(APPEND FTDI_RUNTIME_DLLS "${FTDI_RUNTIME_DLL}")
    add_custom_command(TARGET FT710Dashboard POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FTDI_RUNTIME_DLL}" "$<TARGET_FILE_DIR:FT710Dashboard>")
  endforeach()
  install(TARGETS FT710Dashboard RUNTIME DESTINATION .)
  install(FILES ${HAMLIB_DLLS} DESTINATION .)
  install(FILES ${FTDI_RUNTIME_DLLS} DESTINATION .)
  install(FILES README.md DESTINATION .)
  install(FILES scripts/check-runtime.ps1 DESTINATION .)
  install(FILES "${HAMLIB_ROOT}/COPYING.LIB.txt" "${HAMLIB_ROOT}/LICENSE.txt" "${HAMLIB_ROOT}/AUTHORS.txt" DESTINATION licenses/hamlib)
  install(FILES "${PORTAUDIO_ROOT}/LICENSE.txt" DESTINATION licenses/portaudio)

else()
  message(FATAL_ERROR "Unsupported platform. Build on macOS or Windows.")
endif()
