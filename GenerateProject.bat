@echo off
if not exist "build" md "build"
pushd build
cmake .. -G"Visual Studio 16 2019" -A x64 -DBUILD_TESTING=ON "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"
popd
