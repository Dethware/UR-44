qt_add_library(shared_ui STATIC
    CustomMessageBox.cpp
    CustomMessageBox.h
)
target_include_directories(shared_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shared_ui PUBLIC Qt6::Widgets)
