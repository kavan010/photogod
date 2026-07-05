cmake_minimum_required(VERSION 3.16)
project(photogod VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(photogod
  src/main.cpp
  src/MainWindow.cpp
  src/Canvas.cpp
  src/Document.cpp
  src/Layer.cpp
  src/Filters.cpp
  src/Commands.cpp
  src/Panels.cpp
  src/Dialogs.cpp
)

target_link_libraries(photogod PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
  set_target_properties(photogod PROPERTIES WIN32_EXECUTABLE ON)
endif()
