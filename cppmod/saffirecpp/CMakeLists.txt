PROJECT(saffirecpp)
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)


#
# Saffire-cpp
#

SET(saffire_cpp_SRCS
    saffire.cc
    object.cc
    module.cc
)
include_directories("..")
ADD_LIBRARY(saffire_cpp SHARED ${saffire_cpp_SRCS})
