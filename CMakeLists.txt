cmake_minimum_required(VERSION 3.24)
project(gt_api_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets PrintSupport CONFIG REQUIRED)
# qcustomplot ships as a qmake-built DLL + import libs (no CMake config)
set(QCP_ROOT "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows")
add_library(qcustomplot::qcustomplot SHARED IMPORTED)
set_target_properties(qcustomplot::qcustomplot PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${QCP_ROOT}/include"
  INTERFACE_COMPILE_DEFINITIONS "QCUSTOMPLOT_USE_LIBRARY"
  IMPORTED_IMPLIB_DEBUG   "${QCP_ROOT}/debug/lib/qcustomplotd2.lib"
  IMPORTED_IMPLIB_RELEASE "${QCP_ROOT}/lib/qcustomplot2.lib"
  IMPORTED_LOCATION_DEBUG   "${QCP_ROOT}/debug/bin/qcustomplotd2.dll"
  IMPORTED_LOCATION_RELEASE "${QCP_ROOT}/bin/qcustomplot2.dll")

add_executable(gt_api_test
  src/main.cpp
  src/MainWindow.cpp
  src/Telemetry.cpp
)

if(TARGET qcustomplot::qcustomplot)
  target_link_libraries(gt_api_test PRIVATE Qt6::Widgets Qt6::PrintSupport qcustomplot::qcustomplot)
else()
  target_link_libraries(gt_api_test PRIVATE Qt6::Widgets Qt6::PrintSupport qcustomplot)
endif()

# app-local Qt runtime for running straight out of the build tree
find_program(WINDEPLOYQT_EXE NAMES windeployqt windeployqt.exe
  HINTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/Qt6/bin"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/qtbase/bin")
if(WINDEPLOYQT_EXE)
  add_custom_command(TARGET gt_api_test POST_BUILD
    COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-compiler-runtime "$<TARGET_FILE:gt_api_test>"
    COMMENT "windeployqt: copying Qt runtime")
endif()

# copy qcustomplot DLL + Qt platform plugins next to the exe
add_custom_command(TARGET gt_api_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:qcustomplot::qcustomplot>" "$<TARGET_FILE_DIR:gt_api_test>"
  COMMENT "copy qcustomplot DLL")
set(_QT_PLUGINS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins")
foreach(_pd platforms styles imageformats)
  if(EXISTS "${_QT_PLUGINS}/${_pd}")
    add_custom_command(TARGET gt_api_test POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${_QT_PLUGINS}/${_pd}" "$<TARGET_FILE_DIR:gt_api_test>/${_pd}"
      COMMENT "copy Qt ${_pd} plugins")
  endif()
endforeach()
