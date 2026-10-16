# Desktop integration, named after the application ID
configure_file(${UR44_APP_ID}.metainfo.xml.in ${CMAKE_CURRENT_BINARY_DIR}/${UR44_APP_ID}.metainfo.xml @ONLY)

install(FILES ${UR44_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${UR44_APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${UR44_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
