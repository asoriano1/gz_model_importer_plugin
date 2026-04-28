cmake_minimum_required(VERSION 3.16)
project(robot_importer_gui CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)

# ---- Gazebo Harmonic (via ROS 2 Jazzy vendor packages) ----
find_package(gz-gui8        REQUIRED)
find_package(gz-sim8        REQUIRED COMPONENTS gui)
find_package(gz-transport13 REQUIRED)
find_package(gz-msgs10      REQUIRED)
find_package(gz-rendering8  REQUIRED)
find_package(sdformat14     REQUIRED)

# ---- Qt 5 (used by gz-gui 8) ----
# CMP0100 = NEW: let AUTOMOC process .hh files (our header convention).
if(POLICY CMP0100)
  cmake_policy(SET CMP0100 NEW)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Concurrent Quick QuickControls2)

# ===================================================================
# Plugin target
# Convention: class RobotImporterGui  →  libRobotImporterGui.so
#             QML resource at         :/RobotImporterGui/RobotImporterGui.qml
# ===================================================================
set(PLUGIN_NAME RobotImporterGui)

set(PLUGIN_SOURCES
  src/ImporterPlugin.cpp
  src/ImporterBackend.cpp
  src/ImportOptions.cpp
  src/FileSelector.cpp
  src/FileLoader.cpp
  src/XacroExpander.cpp
  src/UrdfToSdf.cpp
  src/ModelLoader.cpp
  src/InstanceRewriter.cpp
  src/SdfUriRewriter.cpp
  src/PreviewController.cpp
  src/GzSpawnClient.cpp
  src/SdfPreflightChecker.cpp
  src/RuntimeHintAnalyzer.cpp
)

# Headers that carry Q_OBJECT must be listed for AUTOMOC with CMP0100 NEW.
set(PLUGIN_MOC_HEADERS
  include/robot_importer_gui/ImporterPlugin.hh
  include/robot_importer_gui/ImporterBackend.hh
  include/robot_importer_gui/ImportOptions.hh
  include/robot_importer_gui/FileSelector.hh
  include/robot_importer_gui/XacroExpander.hh
  include/robot_importer_gui/ModelLoader.hh
  include/robot_importer_gui/PreviewController.hh
  include/robot_importer_gui/GzSpawnClient.hh
)

# Name MUST match the .qrc filename (without extension) to get a unique
# qInitResources_<name>() symbol. "resources.qrc" collides with gz-gui8's
# own symbol; "RobotImporterGui.qrc" produces a unique mangled name.
qt5_add_resources(PLUGIN_RCC RobotImporterGui.qrc)

add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
  ${PLUGIN_MOC_HEADERS}
  ${PLUGIN_RCC}
)

target_include_directories(${PLUGIN_NAME}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PLUGIN_NAME}
  PRIVATE
    gz-gui8::gz-gui8
    gz-sim8::gz-sim8
    gz-sim8::gz-sim8-gui
    gz-transport13::gz-transport13
    gz-msgs10::gz-msgs10
    gz-rendering8::gz-rendering8
    sdformat14::sdformat14
    Qt5::Core
    Qt5::Concurrent
    Qt5::Quick
    Qt5::QuickControls2
    tinyxml2
    ament_index_cpp::ament_index_cpp
)

# ---- Install ----
install(TARGETS ${PLUGIN_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY gui/
  DESTINATION share/${PROJECT_NAME}/gui
  FILES_MATCHING PATTERN "*.qml"
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
  OPTIONAL
)

ament_environment_hooks(
  "${CMAKE_CURRENT_SOURCE_DIR}/env-hooks/robot_importer_gui.sh.in"
)

ament_package()
