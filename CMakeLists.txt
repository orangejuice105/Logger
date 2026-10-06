# 1. 声明所需的 CMake 最低版本
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称
project(MyLoggerProject)

# 3. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 包含头文件目录（关键！告诉编译器去 include 文件夹找 .h）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 5. 自动搜集 src 目录下所有的 .cpp 文件
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

# 6. 生成可执行文件（名字叫 main，由 SOURCES 里的所有文件编译而成）
add_executable(main ${SOURCES})