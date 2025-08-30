cmake_minimum_required(VERSION 3.16)

project(HoverLogAnalyzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt Widgets
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 项目源文件
set(PROJECT_SOURCES
    main.cpp
    PressAnalyzer.cpp
    PressAnalyzer.h
    PressAnalyzer.ui
    icon.qrc          # 窗口图标资源
)

# macOS Dock 图标 (.icns)
set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/logo.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

# 可执行文件
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HoverLogAnalyzer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${APP_ICON_MACOS}   # 加入 Dock 图标
    )
else()
    if(ANDROID)
        add_library(HoverLogAnalyzer SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(HoverLogAnalyzer
            ${PROJECT_SOURCES}
            ${APP_ICON_MACOS}
            SearchResultHighlighter.h
            BatteryChartWidget.h
            SocTempChartWidget.h
            DockToggleButton.h

            ModuleUsageChart.h
            # 加入 Dock 图标
        )
    endif()
endif()

target_link_libraries(HoverLogAnalyzer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Bundle ID（Qt < 6.1 手动设置）
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.HoverLogAnalyzer)
endif()

# 设置 macOS bundle 属性
set_target_properties(HoverLogAnalyzer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_ICON_FILE logo         # 只写文件名，不写路径
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# 安装规则
include(GNUInstallDirs)
install(TARGETS HoverLogAnalyzer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Qt6 执行最终化
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HoverLogAnalyzer)
endif()

