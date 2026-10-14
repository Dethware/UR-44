find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

# The UR44 protocol, parameter table and device connection (tested, no UI)
qt_add_library(ur44 STATIC
    Protocol.h
    Protocol.cpp

    BulkDump.h
    BulkDump.cpp

    Parameters.h
    Parameters.cpp

    ReverbTime.h
    ReverbTime.cpp

    Transport.h

    AlsaTransport.h
    AlsaTransport.cpp

    Device.h
    Device.cpp
)

# The parameter table is generated from the reverse engineering results in tools/
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(UR44_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools)
set(UR44_PARAMETER_TABLE ${CMAKE_CURRENT_BINARY_DIR}/ParameterTable.cpp)
add_custom_command(
    OUTPUT ${UR44_PARAMETER_TABLE}
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/gen_parameter_table.py
            ${UR44_TOOLS_DIR}/ur44_params.py ${UR44_TOOLS_DIR}/ur44_labels.json ${UR44_PARAMETER_TABLE}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_parameter_table.py
            ${UR44_TOOLS_DIR}/ur44_params.py ${UR44_TOOLS_DIR}/ur44_labels.json
    COMMENT "Generating the UR44 parameter table"
    VERBATIM
)
# Plain data, nothing for moc/uic: keeping it out of autogen also keeps the command attached to one target only
set_source_files_properties(${UR44_PARAMETER_TABLE} PROPERTIES SKIP_AUTOGEN ON)
target_sources(ur44 PRIVATE ${UR44_PARAMETER_TABLE})
target_include_directories(ur44 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ur44 PUBLIC Qt6::Core PRIVATE PkgConfig::ALSA)
