@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0backend"
if not exist ".\build\hnutong_server.exe" (
  echo [错误] 未找到 .\build\hnutong_server.exe，请先在 backend 目录使用 CMake 构建后端！
  pause
  exit /b 1
)
if not exist "config\config.json" copy /Y "config\config.example.json" "config\config.json" >nul
echo 正在启动海大通 (HnuTong) 本地服务端（仅监听本机回环地址），浏览器请访问: http://127.0.0.1:8788
start "" /b cmd /c "timeout /t 2 /nobreak >nul && start http://127.0.0.1:8788"
.\build\hnutong_server.exe -c config\config.json
pause
