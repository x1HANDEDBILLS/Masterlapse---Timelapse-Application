cmake_minimum_required(VERSION 3.16)
project(MasterLapse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Added Multimedia component for QMediaDevices
find_package(Qt6 COMPONENTS Widgets Multimedia REQUIRED)
find_package(OpenCV REQUIRED)

file(GLOB_RECURSE SRC_FILES 
    "src/buffer/*.cpp" 
    "src/camera/*.cpp" 
    "src/process/*.cpp" 
    "src/save/*.cpp" 
    "src/settings/*.cpp" 
    "src/video-encoder/*.cpp"
    "src/gui/windows/*.cpp"
    "src/gui/core/*.cpp"
)

file(GLOB_RECURSE HDR_FILES "include/*.h")

add_executable(Masterlapse main.cpp ${SRC_FILES} ${HDR_FILES})

target_include_directories(Masterlapse PRIVATE include)
target_link_libraries(Masterlapse PRIVATE Qt6::Widgets Qt6::Multimedia ${OpenCV_LIBS})



# Universal Include Root
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")


target_link_libraries(Masterlapse PRIVATE curl)

