cmake_minimum_required(VERSION 3.13)
project(peak_scanner)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(peak_scanner PRIVATE "src/")