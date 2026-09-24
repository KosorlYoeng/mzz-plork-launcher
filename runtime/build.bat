@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

set SRC=%~dp0src
set OBJ=%~dp0build
set OUT=%~dp0..\dist\runtime

if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"

cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE ^
  /Fo"%OBJ%\\" /Fe"%OUT%\MzzPlorkClient.exe" ^
  "%SRC%\main.cpp" "%SRC%\Runtime.cpp" "%SRC%\Lifecycle.cpp" "%SRC%\ClientState.cpp" ^
  "%SRC%\Config.cpp" "%SRC%\Json.cpp" "%SRC%\Log.cpp" "%SRC%\ErrorHandling.cpp" ^
  "%SRC%\Sha256.cpp" "%SRC%\CacheService.cpp" "%SRC%\NetworkService.cpp" ^
  "%SRC%\TcpServerConnection.cpp" "%SRC%\ProtocolClient.cpp" "%SRC%\EventSystem.cpp" ^
  "%SRC%\DownloadState.cpp" "%SRC%\HttpDownloader.cpp" ^
  "%SRC%\ResourceState.cpp" "%SRC%\ResourceRegistry.cpp" "%SRC%\ResourceManager.cpp" ^
  "%SRC%\Entity.cpp" "%SRC%\Player.cpp" "%SRC%\EntityManager.cpp" ^
  "%SRC%\NetworkState.cpp" "%SRC%\Interpolation.cpp" "%SRC%\Replication.cpp" ^
  "%SRC%\GameIntegration.cpp" "%SRC%\ServerConnectionStub.cpp" "%SRC%\ClientRuntimeInterface.cpp"

exit /b %ERRORLEVEL%



