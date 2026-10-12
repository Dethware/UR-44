# add_qt_test(<name> <libraries...>)
function(add_qt_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# shared
add_qt_test(tst_JsonSerialization shared)

# application: the settings are tested on their own, the application isn't a library
add_qt_test(tst_Settings shared)
target_sources(tst_Settings PRIVATE ${PROJECT_SOURCE_DIR}/src/application/Settings.cpp)
target_include_directories(tst_Settings PRIVATE ${PROJECT_SOURCE_DIR}/src/application)
