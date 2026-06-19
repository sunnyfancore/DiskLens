cmake_minimum_required(VERSION 3.24)

project(DiskLens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_QT_UI "Build the Qt Widgets market-facing user interface." OFF)

add_executable(DiskLensNative WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/App.rc
    src/core/DirectoryScanner.cpp
    src/core/FileHasher.cpp
    src/core/Format.cpp
    src/core/LongPath.cpp
    src/core/NtfsMftScanner.cpp
)

target_include_directories(DiskLensNative PRIVATE src)

set_target_properties(DiskLensNative PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_definitions(DiskLensNative PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(DiskLensNative PRIVATE
    Comctl32
    Shlwapi
)

if (MSVC)
    target_compile_options(DiskLensNative PRIVATE /W4 /permissive- /utf-8)
    target_link_options(DiskLensNative PRIVATE /MANIFEST:NO)
endif()

if (BUILD_QT_UI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_executable(DiskLens WIN32
        src/qt/main.cpp
        src/qt/AppIcons.cpp
        src/qt/MainWindow.cpp
        src/qt/ResultTableModel.cpp
        src/qt/TreemapWidget.cpp
        src/qt/resources.qrc
        src/qt/QtApp.rc
        src/core/DirectoryScanner.cpp
        src/core/FileHasher.cpp
        src/core/Format.cpp
        src/core/LongPath.cpp
        src/core/NtfsMftScanner.cpp
    )

    target_include_directories(DiskLens PRIVATE src)
    target_compile_definitions(DiskLens PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(DiskLens PRIVATE
        Qt6::Widgets
        Shlwapi
        dwmapi
    )
    set_target_properties(DiskLens PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        OUTPUT_NAME "DiskLens"
    )

    if (MSVC)
        target_compile_options(DiskLens PRIVATE /W4 /permissive- /utf-8)
        # 清单改由 QtApp.rc 以 RT_MANIFEST 资源嵌入(含 UAC requireAdministrator),链接器不再自出清单片段。
        target_link_options(DiskLens PRIVATE /MANIFEST:NO)
        # NMake Makefiles 在链接命令过长时会写入临时响应文件;中文 OUTPUT_NAME 经该响应文件传给 link.exe 时
        # 编码失真,引发 LNK1104(ASCII 名的 DiskLensNative 因命令行短、不生成响应文件而正常链接)。
        # 故链接产物改用 ASCII 名,构建完成后由 cmake -E(内部宽字符实现)重命名为"磁盘洞察.exe",交付名不变。
        add_custom_command(TARGET DiskLens POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:DiskLens>/磁盘洞察.exe"
            COMMAND "${CMAKE_COMMAND}" -E rename "$<TARGET_FILE:DiskLens>" "$<TARGET_FILE_DIR:DiskLens>/磁盘洞察.exe"
            COMMENT "重命名 DiskLens.exe -> 磁盘洞察.exe"
        )
    endif()
endif()
