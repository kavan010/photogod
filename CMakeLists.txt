cmake_minimum_required(VERSION 3.16)
project(photogod VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

add_executable(photogod
  resources/icons.qrc
  src/main.cpp
  src/MainWindow.cpp
  src/Canvas.cpp
  src/Document.cpp
  src/Layer.cpp
  src/Filters.cpp
  src/Commands.cpp
  src/Panels.cpp
  src/Dialogs.cpp
  src/HomePage.cpp
  src/CommandPalette.cpp
  src/Dock.cpp
  src/HistoryPanel.cpp
)

target_link_libraries(photogod PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
  set_target_properties(photogod PROPERTIES WIN32_EXECUTABLE ON)

  find_program(WINDRES_EXECUTABLE NAMES windres)
  if(WINDRES_EXECUTABLE)
    set(APP_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_RC}" "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app-icon.ico\"\n")
    set(APP_RC_OBJ "${CMAKE_CURRENT_BINARY_DIR}/app_icon.o")
    add_custom_command(
      OUTPUT "${APP_RC_OBJ}"
      COMMAND ${WINDRES_EXECUTABLE} -O coff -i "${APP_RC}" -o "${APP_RC_OBJ}"
      DEPENDS "${APP_RC}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app-icon.ico"
    )
    target_sources(photogod PRIVATE "${APP_RC_OBJ}")
  endif()
endif()

if(APPLE)
  set(ICNS_FILE "${CMAKE_CURRENT_BINARY_DIR}/photogod.icns")
  find_program(ICONUTIL_EXECUTABLE NAMES iconutil)
  if(ICONUTIL_EXECUTABLE)
    add_custom_command(
      OUTPUT "${ICNS_FILE}"
      COMMAND ${ICONUTIL_EXECUTABLE} -c icns
              "${CMAKE_CURRENT_SOURCE_DIR}/resources/appicon/photogod.iconset"
              -o "${ICNS_FILE}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/appicon/photogod.iconset"
    )
    add_custom_target(photogod_icns DEPENDS "${ICNS_FILE}")
    add_dependencies(photogod photogod_icns)
    set_source_files_properties("${ICNS_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(photogod PRIVATE "${ICNS_FILE}")
  endif()

  set_target_properties(photogod PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "PhotoGod"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.photogod.app"
    MACOSX_BUNDLE_ICON_FILE "photogod.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  )
endif()

if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS photogod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES resources/photogod.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/appicon/icon_256.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
          RENAME photogod.png)
  install(FILES resources/appicon/icon_128.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
          RENAME photogod.png)
  install(FILES resources/appicon/icon_64.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps
          RENAME photogod.png)
  install(FILES resources/appicon/icon_48.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
          RENAME photogod.png)
  install(FILES resources/appicon/icon_32.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
          RENAME photogod.png)
endif()
