cmake_minimum_required(VERSION 3.20)
project(qchat CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -pedantic)

add_executable(qchat_server
	server_main.cpp
	server.cpp
	qhash.cpp
)

target_include_directories(qchat_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(qchat_client
	client_main.cpp
	client.cpp
	tui.cpp
	qhash.cpp
)

target_include_directories(qchat_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qchat_client PRIVATE pthread)
