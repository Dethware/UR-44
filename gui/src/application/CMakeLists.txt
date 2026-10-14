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
)

target_link_libraries(ur44-control PRIVATE ur44 shared shared_ui Qt6::Widgets)

# Install the executable
install(TARGETS ur44-control DESTINATION bin)
