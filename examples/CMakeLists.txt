cmake_minimum_required(VERSION 3.16)

set(AUDD_EXAMPLES
    recognize_url
    recognize_file
    recognize_enterprise
    streams_setup
    streams_callback_handler
    streams_longpoll
    custom_catalog_add
    scan_and_rename
    stream_to_csv
)

foreach(ex ${AUDD_EXAMPLES})
    add_executable(${ex} ${ex}.c)
    target_link_libraries(${ex} PRIVATE audd)
    target_include_directories(${ex} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${ex} PRIVATE c_std_99)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${ex} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()
