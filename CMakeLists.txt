cmake_minimum_required(VERSION 3.10)
project(WebServer)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_DEBUG_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()
message(STATUS "CMAKE_CXX_FLAGS = " ${CMAKE_CXX_FLAGS})

set(SOURCE_FILES
        main.cpp
        ./timer/heap_timer.cpp
        ./http/http_conn.cpp
        ./buffer/buffer.cpp
        ./log/log.cpp
        ./CGImysql/sql_connection_pool.cpp
        ./webserver/webserver.cpp
        config.cpp)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR})
add_executable(server ${SOURCE_FILES})
target_link_libraries(server pthread mysqlclient)
