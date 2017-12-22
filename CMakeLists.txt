cmake_minimum_required(VERSION 2.8)
project(cross-platform-utils)

set (CMAKE_CXX_STANDARD 11)

#platform dependent FLAGS
if (UNIX)
  message(status "Setting GCC flags")
  #need pthread because C++11 on Linux depends on it
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  
  #SDL related settings
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fmessage-length=0 -fPIE -fstack-protector-all -Wstack-protector -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -fPIC")
else()
  message(status "Setting MSVC flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHc-")
endif()

#include(GNUInstallDirs)
#message(status "** CMAKE_INSTALL_LIBDIR: ${CMAKE_INSTALL_LIBDIR}")
#message(status "** CMAKE_INSTALL_INCLUDEDIR: ${CMAKE_INSTALL_INCLUDEDIR}")

message(status "** CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

set(TARGET_LIB "cross-platform-utils")

add_library(${TARGET_LIB} STATIC 
				src/ipc.cpp 
				inc/ipc.h 
				inc/utils.h)
target_include_directories(${TARGET_LIB} PUBLIC inc)

set_target_properties(${TARGET_LIB} PROPERTIES PUBLIC_HEADER "inc/ipc.h;inc/utils.h")

install (TARGETS ${TARGET_LIB}
         RUNTIME DESTINATION 		${PROJECT_SOURCE_DIR}/_install
         LIBRARY DESTINATION 		${PROJECT_SOURCE_DIR}/_install/lib
		 ARCHIVE DESTINATION 		${PROJECT_SOURCE_DIR}/_install/lib
		 PUBLIC_HEADER DESTINATION 	${PROJECT_SOURCE_DIR}/_install/include)

add_subdirectory(test)

