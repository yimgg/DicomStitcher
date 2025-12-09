cmake_minimum_required(VERSION 3.16)

project(myDicomViewer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 设置 VTK 和 ITK 的安装路径
set(VTK_DIR "C:/Program Files/VTK/lib/cmake/vtk-9.2" CACHE PATH "VTK 9.2 安装路径")
set(ITK_DIR "C:/Program Files/ITK/lib/cmake/ITK-5.3" CACHE PATH "ITK 5.3 安装路径")

# 查找 VTK 9.2（指定常用组件）
find_package(VTK 9.2 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    RenderingCore
    RenderingOpenGL2
    InteractionStyle
    InteractionImage
    GUISupportQt
    RenderingFreeType
    RenderingGL2PSOpenGL2
    FiltersModeling
    RenderingAnnotation
)
if(VTK_FOUND)
    message(STATUS "VTK version: ${VTK_VERSION}")
    message(STATUS "VTK found at: ${VTK_DIR}")
endif()

# 查找 ITK 5.3
find_package(ITK 5.3 REQUIRED)
if(ITK_FOUND)
    message(STATUS "ITK version: ${ITK_VERSION}")
    message(STATUS "ITK found at: ${ITK_DIR}")
endif()

# 包含 ITK 的模块（VTK 9.2 使用新的模块系统，不需要 VTK_USE_FILE）
include(${ITK_USE_FILE})

set(PROJECT_SOURCES
        main.cpp
        widget.cpp
        widget.h
        widget.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(myDicomViewer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET myDicomViewer APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(myDicomViewer SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(myDicomViewer
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(myDicomViewer PRIVATE 
    Qt${QT_VERSION_MAJOR}::Widgets
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::RenderingCore
    VTK::RenderingOpenGL2
    VTK::InteractionStyle
    VTK::InteractionImage
    VTK::GUISupportQt
    VTK::RenderingFreeType
    VTK::RenderingGL2PSOpenGL2
    VTK::FiltersModeling
    VTK::RenderingAnnotation
    ${ITK_LIBRARIES}
)

# VTK 9.2 OpenGL 初始化说明：
# 在源文件（如 main.cpp 或 widget.cpp）的开头添加以下代码：
# #include <vtkAutoInit.h>
# VTK_MODULE_INIT(vtkRenderingOpenGL2);
# VTK_MODULE_INIT(vtkInteractionStyle);
# VTK_MODULE_INIT(vtkRenderingFreeType);

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.myDicomViewer)
endif()
set_target_properties(myDicomViewer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS myDicomViewer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(myDicomViewer)
endif()