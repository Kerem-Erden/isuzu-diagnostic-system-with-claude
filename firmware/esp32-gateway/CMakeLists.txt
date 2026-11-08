# ESP-IDF project for the Isuzu diagnostic gateway (target: esp32, ESP-IDF 6.0.2).
# Build only – flashing is a separate, explicit step (docs/build-and-release.md).
cmake_minimum_required(VERSION 3.22)

# Reported to the desktop in HELLO as fw=<PROJECT_VER>.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(isuzu_gateway)
