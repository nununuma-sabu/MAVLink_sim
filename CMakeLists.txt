cmake_minimum_required(VERSION 3.16)
project(MAVLink_sim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network OpenGL)
find_package(OpenGL REQUIRED)

# MAVLink ヘッダー (header-only)
set(MAVLINK_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/mavlink)
# MAVLink v2.0 を使用
add_definitions(-DMAVLINK_USE_MESSAGE_INFO)

# ソースファイル
set(CORE_SOURCES
    src/core/DroneState.h
    src/core/DroneSimulator.h
    src/core/DroneSimulator.cpp
    src/core/FlightController.h
    src/core/FlightController.cpp
)

set(MAVLINK_SOURCES
    src/mavlink/MavlinkUdpLink.h
    src/mavlink/MavlinkUdpLink.cpp
    src/mavlink/MavlinkManager.h
    src/mavlink/MavlinkManager.cpp
    src/mavlink/MessageHandler.h
    src/mavlink/MessageHandler.cpp
)

set(GUI_SOURCES
    src/gui/MainWindow.h
    src/gui/MainWindow.cpp
    src/gui/AttitudeIndicator.h
    src/gui/AttitudeIndicator.cpp
    src/gui/TelemetryPanel.h
    src/gui/TelemetryPanel.cpp
    src/gui/MapView.h
    src/gui/MapView.cpp
    src/gui/MapView3D.h
    src/gui/MapView3D.cpp
    src/gui/ControlPanel.h
    src/gui/ControlPanel.cpp
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    ${CORE_SOURCES}
    ${MAVLINK_SOURCES}
    ${GUI_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MAVLINK_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
    Qt5::OpenGL
    OpenGL::GL
)

# インストール設定
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
