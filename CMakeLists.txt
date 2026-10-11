cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OMACAP_VERSION LIMIT_COUNT 1)
project(omacap VERSION ${OMACAP_VERSION} LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
  Concurrent Core Gui Widgets Qml Quick QuickControls2 Multimedia OpenGL Network Test)

# Contracts between the capture overlay and the video process.
add_library(omacap-ipc STATIC
  src/record-request.cpp src/record-request.h
  src/video-ipc.cpp src/video-ipc.h
)
target_include_directories(omacap-ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(omacap-ipc PUBLIC cxx_std_23)
target_link_libraries(omacap-ipc PUBLIC Qt6::Core Qt6::Network)

# Capture overlay, screenshot editor, previews and pins, derived from Omasnap.
add_subdirectory(snap)

# The video editor: recording, edit model, timeline and exports.
add_library(omacap-video STATIC
  src/backend.cpp src/backend.h
  src/exporter.cpp src/exporter.h
  src/frames.h
  src/model.cpp src/model.h
  src/theme.cpp src/theme.h
  src/video-owner.cpp src/video-owner.h
  src/waveforms.cpp src/waveforms.h
)
target_include_directories(omacap-video PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(omacap-video PUBLIC cxx_std_23)
target_compile_options(omacap-video PRIVATE -Wall -Wextra)
target_link_libraries(omacap-video PUBLIC omacap-ipc
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
  Qt6::Multimedia Qt6::OpenGL Qt6::Network)

qt_add_executable(omacap
  src/main.cpp
  src/video-app.cpp src/video-app.h
  resources.qrc
)
target_compile_options(omacap PRIVATE -Wall -Wextra)
target_compile_definitions(omacap PRIVATE OMACAP_VERSION="${PROJECT_VERSION}")
target_link_libraries(omacap PRIVATE omacap-video omasnap-core)

if(BUILD_TESTING)
  qt_add_executable(omacap-tests tests/integration.cpp resources.qrc)
  target_link_libraries(omacap-tests PRIVATE omacap-video Qt6::Test)
endif()

install(TARGETS omacap RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/omacap.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/omacap.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omacap")
install(FILES snap/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omacap" RENAME Omasnap-LICENSE)
install(FILES snap/assets/OFL.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omacap" RENAME Neucha-OFL.txt)
install(FILES snap/assets/JetBrainsMono-OFL.txt snap/assets/Inter-OFL.txt snap/assets/Lucide-ISC.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omacap")
