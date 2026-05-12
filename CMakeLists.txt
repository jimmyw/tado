cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tado_cc1101_receiver)

target_sources(app PRIVATE
    src/cc1101.c
    src/main.c
)
