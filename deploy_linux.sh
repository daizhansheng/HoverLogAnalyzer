 #!/bin/bash

# =============================
# 配置区域
# =============================
APP_NAME=HoverLogAnalyzer
BUILD_TYPE=Release
BUILD_DIR=build/build-release

# Qt 路径
QT_ROOT=$HOME/Qt/5.15.1/gcc_64
QMAKE_PATH=$QT_ROOT/bin/qmake

# linuxdeployqt
DEPLOYQT=linuxdeployqt-continuous-x86_64.AppImage

# C++ 标准库路径 (请确保此路径正确!)
LIBSTDCXX_PATH="/usr/lib/x86_64-linux-gnu/libstdc++.so.6"

# =============================
# Step 1: 准备工具
# =============================
if [ ! -f $DEPLOYQT ]; then
    wget https://github.com/probonopd/linuxdeployqt/releases/download/continuous/$DEPLOYQT
    chmod +x $DEPLOYQT
fi

# =============================
# Step 2: 构建
# =============================
mkdir -p $BUILD_DIR
cd $BUILD_DIR
rm -rf CMakeCache.txt CMakeFiles

cmake ../.. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_PREFIX_PATH=$QT_ROOT \
    -DQt5Charts_DIR=$QT_ROOT/lib/cmake/Qt5Charts

cmake --build . -j$(nproc)
cd ../../

APP_BIN="$BUILD_DIR/$APP_NAME"
if [ ! -f "$APP_BIN" ]; then
    echo "❌ 构建失败，未找到可执行文件"
    exit 1
fi

# =============================
# Step 3: 准备 Deploy 目录
# =============================
rm -rf deploy
mkdir -p deploy
cp "$APP_BIN" deploy/

# 【修复 1】: 强制复制 libstdc++.so.6 (解决 _ZdlPvm 错误)
if [ -f "$LIBSTDCXX_PATH" ]; then
    cp "$LIBSTDCXX_PATH" deploy/
    echo "✅ 已捆绑 libstdc++.so.6"
else
    echo "❌ 错误：未找到 libstdc++.so.6"
    exit 1
fi

# 【修复 2】: 创建标准的 .desktop 文件 (解决 Categories 错误)
# 如果项目根目录没有图标，这里 Icon 会指向空，建议准备一个 icon.png 放进去
echo ">>> 创建 .desktop 文件..."
cat <<EOF > deploy/default.desktop
[Desktop Entry]
Type=Application
Name=$APP_NAME
Exec=$APP_NAME
Icon=default
Comment=Log Analysis Tool
Terminal=false
Categories=Utility;Development;
EOF

# =============================
# Step 4: 打包
# =============================
echo ">>> 开始打包..."

# 【修复 3】: 设置 PATH 让 linuxdeployqt 找到 lconvert
export PATH=$QT_ROOT/bin:$PATH
export QMAKE=$QMAKE_PATH

# 使用 -no-strip 防止库被破坏
./$DEPLOYQT deploy/$APP_NAME \
    -appimage \
    -bundle-non-qt-libs \
    -qmldir=. \
    -no-strip \
    -verbose=2

echo "🎉 打包流程结束。"
