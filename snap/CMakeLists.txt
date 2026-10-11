# Omasnap-derived core, built as part of OmaCap (see VENDORED.md).
find_package(PkgConfig REQUIRED)
find_package(LayerShellQt REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(Libdeflate REQUIRED IMPORTED_TARGET libdeflate)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_XMLS
  # ext-image-capture-source references this interface even for output-only use.
  /usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
  /usr/share/wayland-protocols/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
  /usr/share/wayland-protocols/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
  ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-virtual-pointer-unstable-v1.xml
)
set(PROTOCOL_SOURCES)
foreach(PROTOCOL_XML IN LISTS PROTOCOL_XMLS)
  get_filename_component(PROTOCOL_NAME "${PROTOCOL_XML}" NAME_WE)
  set(PROTOCOL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL_NAME}-client-protocol.h")
  set(PROTOCOL_CODE "${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL_NAME}-protocol.c")
  add_custom_command(
    OUTPUT "${PROTOCOL_HEADER}" "${PROTOCOL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${PROTOCOL_XML}" "${PROTOCOL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${PROTOCOL_XML}" "${PROTOCOL_CODE}"
    DEPENDS "${PROTOCOL_XML}"
    VERBATIM
  )
  list(APPEND PROTOCOL_SOURCES "${PROTOCOL_HEADER}" "${PROTOCOL_CODE}")
endforeach()

add_library(omasnap-core STATIC
  src/card-stack.hpp
  src/cli-path.cpp
  src/cli-path.hpp
  src/icons.cpp
  src/icons.hpp
  src/instance-lock.cpp
  src/instance-lock.hpp
  src/overlay-dismissal.cpp
  src/overlay-dismissal.hpp
  src/pin-file.cpp
  src/pin-file.hpp
  src/pin-layout.cpp
  src/pin-layout.hpp
  src/png.cpp
  src/png.hpp
  src/capture.cpp
  src/capture.hpp
  src/cut.cpp
  src/cut.hpp
  src/palette-config.cpp
  src/palette-config.hpp
  src/backdrop-assets.cpp
  src/backdrop-assets.hpp
  src/capture-state.cpp
  src/capture-state.hpp
  src/look.cpp
  src/look.hpp
  src/recent-snaps.cpp
  src/recent-snaps.hpp
  src/output-config.cpp
  src/output-config.hpp
  src/pin-expiry.cpp
  src/pin-expiry.hpp
  src/overlay-chrome.cpp
  src/overlay-chrome.hpp
  src/shortcut-guide.cpp
  src/shortcut-guide.hpp
  src/chrome-theme.cpp
  src/chrome-theme.hpp
  src/eyedropper.cpp
  src/eyedropper.hpp
  src/surface-capture.cpp
  src/stitch.cpp
  src/stitch.hpp
  src/text-band.cpp
  src/text-band.hpp
  src/auto-capture.cpp
  src/auto-capture.hpp
  src/scroll-capture.cpp
  src/scroll-capture.hpp
  src/scroll-inject.cpp
  src/scroll-inject.hpp
  src/startup-timing.cpp
  src/startup-timing.hpp
  src/stroke-smoothing.cpp
  src/stroke-smoothing.hpp
  ${PROTOCOL_SOURCES}
  src/editor.cpp
  src/editor-save-as.cpp
  src/editor.hpp
  src/main.cpp
  src/snap-main.hpp
  src/pin.cpp
  src/pin.hpp
)
qt_add_resources(omasnap-core "capture-fonts"
  PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/Neucha.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/JetBrainsMono-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/InterDisplay-SemiBold.ttf"
)

# CXX-only: omasnap-core also compiles Wayland protocol .c files.
target_precompile_headers(omasnap-core PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:<QtWidgets/QWidget$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtWidgets/QApplication$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QPainter$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QPainterPath$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QImage$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QPixmap$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QFontDatabase$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QMouseEvent$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QKeyEvent$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtGui/QWheelEvent$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtConcurrent/QtConcurrentRun$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QFutureWatcher$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QProcess$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QFile$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QDir$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QFileInfo$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QJsonObject$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QJsonArray$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QJsonDocument$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QTimer$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QString$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<QtCore/QVector$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>"
  "$<$<COMPILE_LANGUAGE:CXX>:<cmath$<ANGLE-R>>"
)

target_include_directories(omasnap-core PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(omasnap-core PUBLIC cxx_std_23)
target_compile_options(omasnap-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omasnap-core PUBLIC
  omacap-ipc
  Qt6::Concurrent
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  LayerShellQt::Interface
  PkgConfig::WaylandClient
  PkgConfig::Libdeflate
)

target_compile_definitions(omasnap-core PRIVATE OMACAP_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
qt_add_executable(omasnap-smoke
  tests/scroll-focus-smoke.cpp
  tests/scroll-focus-smoke.hpp
  tests/chrome-theme-smoke.cpp
  tests/chrome-theme-smoke.hpp
  tests/editor-smoke.cpp
  tests/recent-persistence-smoke.cpp
  tests/recent-persistence-smoke.hpp
  tests/selection-repaint-smoke.cpp
  tests/selection-repaint-smoke.hpp
  tests/clipboard-smoke.cpp
  tests/clipboard-smoke.hpp
  tests/png-smoke.cpp
  tests/png-smoke.hpp
  tests/instance-lock-smoke.cpp
  tests/instance-lock-smoke.hpp
  tests/transform-smoke.cpp
  tests/transform-smoke.hpp
  tests/cut-smoke.cpp
  tests/cut-smoke.hpp
  tests/cut-mapping-smoke.cpp
  tests/cut-mapping-smoke.hpp
  tests/palette-config-smoke.cpp
  tests/palette-config-smoke.hpp
  tests/stroke-smoothing-smoke.cpp
  tests/stroke-smoothing-smoke.hpp
  src/cli-path.cpp
  src/recent-snaps.cpp
  src/recent-snaps.hpp
  src/cli-path.hpp
  src/capture.cpp
  src/capture.hpp
  src/overlay-chrome.cpp
  src/overlay-chrome.hpp
  tests/surface-capture-smoke.cpp
  tests/surface-capture-smoke.hpp
  tests/pin-lifecycle-smoke.cpp
  tests/pin-lifecycle-smoke.hpp
  tests/pin-interaction-smoke.cpp
  tests/pin-interaction-smoke.hpp
  tests/pin-layout-smoke.cpp
  tests/pin-layout-smoke.hpp
  src/stitch.cpp
  src/stitch.hpp
  src/auto-capture.cpp
  src/auto-capture.hpp
  tests/stitch-smoke.cpp
  tests/stitch-smoke.hpp
  tests/scroll-startup-smoke.cpp
  tests/scroll-startup-smoke.hpp
  tests/save-as-smoke.cpp
  tests/save-as-smoke.hpp
  tests/look-smoke.cpp
  tests/look-smoke.hpp
)
target_include_directories(omasnap-smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)
target_compile_options(omasnap-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omasnap-smoke PRIVATE omasnap-core Qt6::Test)
target_precompile_headers(omasnap-smoke REUSE_FROM omasnap-core)
add_test(NAME omasnap-smoke COMMAND omasnap-smoke)
set_tests_properties(omasnap-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
