cmake_minimum_required(VERSION 3.24)
project(xMind LANGUAGES C CXX)
include(CTest)
# The supported product implementation is C++ with embedded xlang3.
# Runtime/SDK/standard-library source selections are defined in Native/.
add_subdirectory(Native)
