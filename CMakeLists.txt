# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Enable C++ exceptions support (required by VCP drivers)
set(CONFIG_COMPILER_CXX_EXCEPTIONS y)

# Exclude unused components
set(EXCLUDE_COMPONENTS esp_http_client)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usb_host_arduino)

