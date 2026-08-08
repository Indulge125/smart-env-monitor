@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

rem ============================================================
rem  智境边缘网关 - 一键启动（真实串口 + Web 仪表盘）
rem
rem  用法：
rem    双击运行          -> 使用下方默认参数
rem    启动网关.bat COM5 -> 覆盖串口号（其他参数保持默认）
rem
rem  改参数只改下面三个 set 行
rem ============================================================

rem 串口号：设备管理器 - 端口(COM和LPT) 里查看
set "COM=COM8"

rem 波特率：与板子 USART2 一致
set "BAUD=115200"

rem Web 端口：被占用时改成 8081/8082...
set "WEBPORT=8080"

if not "%~1"=="" set "COM=%~1"

if not exist build\Release\zhijing_edge_gateway.exe (
    echo 找不到 build\Release\zhijing_edge_gateway.exe，请先编译：
    echo   cmake -S zhijing_edge_gateway -B build
    echo   cmake --build build --config Release
    echo.
    pause
    exit /b 1
)

echo [1/3] 连接 %COM% %BAUD% ...
echo [2/3] 打开 Web 仪表盘: http://127.0.0.1:%WEBPORT%
echo [3/3] 按 Ctrl+C 优雅退出
echo.
build\Release\zhijing_edge_gateway.exe %COM% %BAUD% --web %WEBPORT%
echo.
pause
