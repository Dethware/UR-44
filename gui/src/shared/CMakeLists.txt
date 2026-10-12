qt_add_library(shared STATIC
    Exception.h

    FileSystem.cpp
    FileSystem.h

    Json.h
    Json.cpp

    JsonSerialization.h

    pathmatcher/FSTreeMatcher.h
    pathmatcher/IPathMatcher.h
    pathmatcher/MultiMatcher.h
    pathmatcher/RegexpMatcher.h
    pathmatcher/SeparatorPrefixTree.h
)
target_include_directories(shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shared PUBLIC Qt6::Core)
