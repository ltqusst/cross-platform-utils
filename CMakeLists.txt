cmake_minimum_required(VERSION 3.2)
project(cross-platform-utils)

set (CMAKE_CXX_STANDARD 11)

include(helper.cmake)

#platform dependent compiler options
if (MSVC)
add_compile_options("/EHc-")
else()
#need pthread because C++11 on Linux depends on it
add_compile_options("-lpthread")
add_compile_options("-Wall")
add_compile_options("-fmessage-length=0")
add_compile_options("-fPIE")
add_compile_options("-fstack-protector-all")
add_compile_options("-Wstack-protector")
add_compile_options("-D_FORTIFY_SOURCE=2")
add_compile_options("-Wformat")
add_compile_options("-Wformat-security")
add_compile_options("-fPIC")
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
				
target_include_directories	(${TARGET_LIB} PUBLIC inc)
target_link_libraries		(${TARGET_LIB} INTERFACE pthread)

set_target_properties(${TARGET_LIB} PROPERTIES PUBLIC_HEADER "inc/ipc.h;inc/utils.h")

install (TARGETS ${TARGET_LIB}
         RUNTIME DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
         LIBRARY DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
		 ARCHIVE DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
		 PUBLIC_HEADER DESTINATION 	${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/include)

add_subdirectory(test)

#print_target_properties(${TARGET_LIB})
#print_target_properties(test-ipc)
enable_testing()
add_test(NAME test_ipc1 COMMAND arg1 arg2 ... )


set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "tingqian.li@intel.com")
include(CPack)
