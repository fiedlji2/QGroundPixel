#!/usr/bin/env bash
# QGroundPixel build environment (Git Bash)
TOOLS="D:/Claude/Code/QGroundPixel/tools"
export QT_ROOT_DIR="$TOOLS/qt/android_arm64_v8a"
export QT_HOST_PATH="$TOOLS/qt/msvc2022_64"
export ANDROID_SDK_ROOT="$TOOLS/android-sdk"
export ANDROID_HOME="$TOOLS/android-sdk"
export ANDROID_NDK="$TOOLS/android-sdk/ndk/27.2.12479018"
export ANDROID_NDK_ROOT="$ANDROID_NDK"
export ANDROID_PLATFORM="android-28"
export JAVA_HOME="$TOOLS/jdk21"
# Git Bash resolves commands only from POSIX-style (/d/...) PATH entries.
export PATH="$(cygpath -u "$TOOLS/cmake/bin"):$(cygpath -u "$TOOLS/ninja"):$(cygpath -u "$JAVA_HOME/bin"):$PATH"
