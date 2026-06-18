#include "app/App.h"

#include <Windows.h>

/**
 * @brief Windows GUI 程序入口。
 * @param instance 当前进程的模块实例句柄。
 * @param previousInstance 旧版 Windows 保留参数，现代系统始终为空。
 * @param commandLine 命令行字符串。
 * @param showCommand 窗口显示命令。
 * @return 进程退出码。
 */
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand) {
    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(commandLine);

    disk_lens::app::App app(instance);
    if (!app.Initialize(showCommand)) {
        MessageBoxW(nullptr, L"初始化窗口失败。", L"磁盘洞察", MB_ICONERROR | MB_OK);
        return 1;
    }

    return app.RunMessageLoop();
}
