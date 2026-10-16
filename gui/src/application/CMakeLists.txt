qt_add_executable(ur44-control
    main.cpp

    Application.h
    Application.cpp

    MainWindow.ui
    MainWindow.cpp
    MainWindow.h

    Settings.h
    Settings.cpp

    Bindings.h
    Bindings.cpp

    Widgets.h
    Widgets.cpp

    DevicePage.h
    DevicePage.cpp

    EffectEditors.h
    EffectEditors.cpp

    LevelMeter.h
    LevelMeter.cpp

    MixPage.h
    MixPage.cpp

    ScenesPanel.h
    ScenesPanel.cpp

    AboutDialog.h
    AboutDialog.cpp
)

target_link_libraries(ur44-control PRIVATE ur44 shared shared_ui Qt6::Widgets)
# One version number, from the project() call
target_compile_definitions(ur44-control PRIVATE UR44_CONTROL_VERSION="${PROJECT_VERSION}" UR44_APP_ID="${UR44_APP_ID}")

install(TARGETS ur44-control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
