PROJECT(exif)
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)



#
# Exif module
#

FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(PC_LIBEXIF REQUIRED libexif)
INCLUDE_DIRECTORIES(${PC_LIBEXIF_INCLUDE_DIRS})
LIST(APPEND 3rdparty_libs ${PC_LIBEXIF_LIBRARIES})

# We must use modexif instead of exif, as the exif library is called 'exif' too.
ADD_LIBRARY(modexif MODULE exif.cc)
TARGET_LINK_LIBRARIES(modexif ${3rdparty_libs})
SET_TARGET_PROPERTIES(modexif PROPERTIES PREFIX "")
# We override modexif.so to be called exif.so as output
SET_TARGET_PROPERTIES(modexif PROPERTIES OUTPUT_NAME "exif")



#
# Saffire-cpp
#

SET(saffire_cpp_SRCS
    saffire.cc
    object.cc
    module.cc
)
ADD_LIBRARY(saffire_cpp SHARED ${saffire_cpp_SRCS})



#
# Test app
#

ADD_EXECUTABLE(test test.c)
TARGET_LINK_LIBRARIES(test dl)
