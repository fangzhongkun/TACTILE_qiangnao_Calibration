 <p align="center">
    <img src="resources/icons/scanner.png"  height="96">
</p>

<h1 align="center">Scanner</h1>

<p>
    <p align="center">
        柔性压力感知系统
    </p>
</p>

## Usage

```bash
git clone https://github.com/ffiirree/scanner.git --recursive

# compilation
cd scanner && mkdir build && cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# package
cpack .\CPackConfig.cmake
```

### Environment

- `C++` :  `C++20`
- `Qt` : `6.7.x`
- `CMake` : `>= 3.22`
- `FFmpeg` : `>= 6.1 on Windows` / `>= 4.2 on Linux`
- `OpenCV` : `>= 4.2`

### Supported Operating Systems

- [x] Windows 10
- [x] Windows 11
- [ ] Ubuntu  22.04
- [ ] Ubuntu  24.04

### Windows

#### Install FFmpeg

从[官网](https://ffmpeg.org/download.html#build-windows)下载编译好的`库版本(ffmpeg-xxxxx-shared.7z)`，添加根目录和bin目录到环境变量中。

已经在项目中写好了CMake查找的配置文件。

#### Compile & Install OpenCV

> 预先安装[Intel oneTBB](https://www.intel.com/content/www/us/en/docs/onetbb/get-started-guide/2021-12/overview.html)

```bash
# Debug
cmake -E env CXXFLAGS="-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" cmake .. -G Ninja -DBUILD_JAVA=OFF -DBUILD_TESTS=OFF -DBUILD_opencv_apps=OFF -DBUILD_opencv_calib3d=OFF -DBUILD_opencv_dnn=OFF -DBUILD_opencv_flann=OFF -DBUILD_opencv_gapi=OFF -DBUILD_opencv_java_bindings_generator=OFF -DBUILD_opencv_js_bindings_generator=OFF -DBUILD_opencv_objc_bindings_generator=OFF -DBUILD_opencv_python_bindings_generator=OFF -DBUILD_opencv_python_tests=OFF -DBUILD_opencv_objdetect=OFF -DBUILD_opencv_ml=OFF -DBUILD_opencv_photo=OFF -DBUILD_opencv_stitching=OFF -DBUILD_opencv_ts=OFF -DBUILD_opencv_video=OFF -DBUILD_opencv_videoio=OFF -DCMAKE_INSTALL_PREFIX=D:/Libs/opencv -DWITH_TBB=ON -DCMAKE_BUILD_TYPE=Debug

cmake --build . --config Debug --target install

# Release
cmake -E env CXXFLAGS="-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" cmake .. -G Ninja -DBUILD_JAVA=OFF -DBUILD_TESTS=OFF -DBUILD_opencv_apps=OFF -DBUILD_opencv_calib3d=OFF -DBUILD_opencv_dnn=OFF -DBUILD_opencv_flann=OFF -DBUILD_opencv_gapi=OFF -DBUILD_opencv_java_bindings_generator=OFF -DBUILD_opencv_js_bindings_generator=OFF -DBUILD_opencv_objc_bindings_generator=OFF -DBUILD_opencv_python_bindings_generator=OFF -DBUILD_opencv_python_tests=OFF -DBUILD_opencv_objdetect=OFF -DBUILD_opencv_ml=OFF -DBUILD_opencv_photo=OFF -DBUILD_opencv_stitching=OFF -DBUILD_opencv_ts=OFF -DBUILD_opencv_video=OFF -DBUILD_opencv_videoio=OFF -DCMAKE_INSTALL_PREFIX=D:/Libs/opencv -DWITH_TBB=ON -DCMAKE_BUILD_TYPE=Release

cmake --build . --config Release --target install
```

#### Install Qt

从[官网](https://download.qt.io/archive/online_installers/4.8/)下载在线安装器。

同样将路径添加到环境变量中，CMake会自动查找。

> 注意，请勾选`Qt Charts`模块。
> CLion Debug: QT_ASSUME_STDERR_HAS_CONSOLE=1
