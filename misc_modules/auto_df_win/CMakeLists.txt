
cmake_minimum_required(VERSION 3.13)
project(auto_df_win)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# Link Windows socket library (must be after include)
if(WIN32)
    target_link_libraries(auto_df_win PRIVATE ws2_32)
endif()