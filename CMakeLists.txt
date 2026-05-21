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
    src/core/GeoUtils.h
    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/MapLocation.h
    src/core/MapLocationLoader.h
    src/core/MapLocationLoader.cpp
    src/core/DroneSimulator.h
    src/core/DroneSimulator.cpp
    src/core/FlightController.h
    src/core/FlightController.cpp
    src/core/MissionManager.h
    src/core/MissionManager.cpp
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
    src/gui/panels/ControlPanel.h
    src/gui/panels/ControlPanel.cpp
    src/gui/panels/TelemetryPanel.h
    src/gui/panels/TelemetryPanel.cpp
    src/gui/panels/MissionPanel.h
    src/gui/panels/MissionPanel.cpp
    src/gui/panels/LogPanel.h
    src/gui/panels/LogPanel.cpp
    src/gui/widgets/AttitudeIndicator.h
    src/gui/widgets/AttitudeIndicator.cpp
    src/gui/widgets/MapView.h
    src/gui/widgets/MapView.cpp
    src/gui/map3d/BuildingLoader.h
    src/gui/map3d/BuildingLoader.cpp
    src/gui/map3d/BuildingProvider.h
    src/gui/map3d/BuildingProvider.cpp
    src/gui/map3d/MapView3D.h
    src/gui/map3d/MapView3D.cpp
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    resources/resources.qrc
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
