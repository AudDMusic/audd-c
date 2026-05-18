cmake_minimum_required(VERSION 3.16)

# Unity vendor lib.
add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

set(AUDD_TEST_FILES
    test_error_mapping.c
    test_url_helpers.c
    test_longpoll_category.c
    test_callback_parsing.c
    test_recognition_parsing.c
    test_enterprise_parsing.c
    test_extras.c
    test_retry_policy.c
)

foreach(t ${AUDD_TEST_FILES})
    get_filename_component(name ${t} NAME_WE)
    add_executable(${name} ${t})
    target_link_libraries(${name} PRIVATE audd_static unity)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/vendor/cJSON
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_features(${name} PRIVATE c_std_99)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endforeach()
