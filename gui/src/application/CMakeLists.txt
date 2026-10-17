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

    SettingsPage.h
    SettingsPage.cpp

    Autostart.h
    Autostart.cpp
)

# The application icon, built in (the tray and the window icon; SVG would need Qt SVG). The PNGs are rendered from
# gui/data/org.dethware.ur44control.svg by gui/data/render_icons.sh, which has the same list of sizes.
qt_add_resources(ur44-control "icons" PREFIX "/icons" BASE ../../data/icons FILES
    ../../data/icons/ur44control-16.png
    ../../data/icons/ur44control-22.png
    ../../data/icons/ur44control-24.png
    ../../data/icons/ur44control-32.png
    ../../data/icons/ur44control-48.png
    ../../data/icons/ur44control-64.png
    ../../data/icons/ur44control-128.png
    ../../data/icons/ur44control-256.png
)

target_link_libraries(ur44-control PRIVATE ur44 shared shared_ui Qt6::Widgets Qt6::Network)
# One version number, from the project() call
target_compile_definitions(ur44-control PRIVATE UR44_CONTROL_VERSION="${PROJECT_VERSION}" UR44_APP_ID="${UR44_APP_ID}")

install(TARGETS ur44-control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
