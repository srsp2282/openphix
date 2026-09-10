cmake_minimum_required(VERSION 3.13)
project(openphix-tool C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  add_compile_options(/W3 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wshadow)
endif()
if(NOT WIN32)
  # nanosleep, clock_gettime, fileno, opendir under strict -std=c99
  add_compile_definitions(_POSIX_C_SOURCE=200809L _DARWIN_C_SOURCE)
endif()

# Protocol, device logic, package handling and the simulator: no USB dependency.
add_library(openphix_core STATIC
  src/protocol.c src/device.c src/package.c src/fake.c src/crypto.c)
target_include_directories(openphix_core PUBLIC src)

# libusb-1.0: pkg-config where available, otherwise LIBUSB_INCLUDE_DIR / LIBUSB_LIBRARY.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
endif()
if(LIBUSB_FOUND)
  set(LIBUSB_TARGET PkgConfig::LIBUSB)
else()
  find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
  find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
  if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "libusb-1.0 not found. Install libusb-1.0-0-dev (Debian/Ubuntu), libusb (brew) or point LIBUSB_INCLUDE_DIR/LIBUSB_LIBRARY at it.")
  endif()
  add_library(libusb_imported UNKNOWN IMPORTED)
  set_target_properties(libusb_imported PROPERTIES
    IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}")
  set(LIBUSB_TARGET libusb_imported)
endif()

add_executable(openphix-tool src/main.c src/usb_transport.c)
target_link_libraries(openphix-tool PRIVATE openphix_core ${LIBUSB_TARGET})

include(GNUInstallDirs)
install(TARGETS openphix-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# On Linux also install the udev rule, so the tool works without root, and
# reload udev unless this is a staged install into DESTDIR.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(OPENPHIX_UDEV_DIR "/etc/udev/rules.d" CACHE PATH "directory for the udev rule")
  install(FILES 99-openphix.rules DESTINATION "${OPENPHIX_UDEV_DIR}")
  install(CODE [[
    if(NOT DEFINED ENV{DESTDIR})
      find_program(UDEVADM udevadm)
      if(UDEVADM)
        execute_process(COMMAND ${UDEVADM} control --reload-rules ERROR_QUIET)
        execute_process(COMMAND ${UDEVADM} trigger --subsystem-match=usb ERROR_QUIET)
        message(STATUS "udev rules reloaded. Replug the tool and it works without sudo.")
      endif()
    endif()
  ]])
endif()

enable_testing()
add_executable(test_protocol tests/test_protocol.c)
target_link_libraries(test_protocol PRIVATE openphix_core)
add_test(NAME protocol COMMAND test_protocol)
add_executable(test_device tests/test_device.c)
target_link_libraries(test_device PRIVATE openphix_core)
add_test(NAME device COMMAND test_device)
add_executable(test_crypto tests/test_crypto.c)
target_link_libraries(test_crypto PRIVATE openphix_core)
add_test(NAME crypto COMMAND test_crypto)
