@echo off
rem QGroundPixel detached APK build (log: D:\Claude\Code\QGroundPixel\build-apk.log)
set TOOLS=D:\Claude\Code\QGroundPixel\tools
set QT_ROOT_DIR=%TOOLS%\qt\android_arm64_v8a
set QT_HOST_PATH=%TOOLS%\qt\msvc2022_64
set ANDROID_SDK_ROOT=%TOOLS%\android-sdk
set ANDROID_HOME=%TOOLS%\android-sdk
set ANDROID_NDK=%TOOLS%\android-sdk\ndk\27.2.12479018
set ANDROID_NDK_ROOT=%ANDROID_NDK%
set ANDROID_PLATFORM=android-28
set JAVA_HOME=%TOOLS%\jdk21
set PATH=%TOOLS%\cmake\bin;%TOOLS%\ninja;%JAVA_HOME%\bin;%PATH%
set GRADLE_OPTS=-Dorg.gradle.daemon=false

cd /d D:\Claude\Code\QGroundPixel\repos\qgroundcontrol
cmake --build --preset Android
if %ERRORLEVEL%==0 (
    echo QGP_BUILD_SUCCESS
) else (
    echo QGP_BUILD_FAILED exit %ERRORLEVEL%
)
