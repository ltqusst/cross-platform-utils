cmake_minimum_required(VERSION 3.2)
project(cross-platform-utils)

set (CMAKE_CXX_STANDARD 11)

include(helper.cmake)

#platform dependent compiler options

if (MSVC)
add_compile_options("/EHc-")
# Tell CMake to set the platform toolset. requires the Win10 SDK and updated onecore.lib
set(CMAKE_VS_PLATFORM_TOOLSET "v141") # Use VS 2015 with Win 10 SDK
set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "10.0.16299.0") # Targets Windows 10. Alt is ${WindowsSDKVersion}
set(CMAKE_CXX_STANDARD_LIBRARIES "") # do not link against standard win32 libs i.e. kernel32, uuid, user32, etc.
else()
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
				src/utils.cpp 
				inc/ipc.h 
				inc/utils.h)

target_include_directories	(${TARGET_LIB} PUBLIC inc)
set_target_properties		(${TARGET_LIB} PROPERTIES PUBLIC_HEADER "inc/ipc.h;inc/utils.h")
if(UNIX)
	# on UNIX lib user must link with pthread because C++11 concurrency needs them
	target_link_libraries		(${TARGET_LIB} INTERFACE pthread)
endif()

install (TARGETS ${TARGET_LIB}
         RUNTIME DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
         LIBRARY DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
		 ARCHIVE DESTINATION 		${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/lib
		 PUBLIC_HEADER DESTINATION 	${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/include)

add_subdirectory(test)

#print_target_properties(${TARGET_LIB})
#print_target_properties(ipc_client)
#print_target_properties(ipc_server)

#message("${TARGET_LIB} is here:" "$<TARGET_FILE:${TARGET_LIB}>")
#message("ipc_client is here   :" "$<TARGET_FILE:ipc_client>")
#message("ipc_server is here   :" "$<TARGET_FILE:ipc_server>")

enable_testing()

add_test(NAME no_connection		COMMAND ipc_client "noconn")
add_test(NAME start_server		COMMAND ipc_client "start" "ipc_server" "conn")
add_test(NAME test_conn			COMMAND ipc_client "conn")
add_test(NAME test_tst1			COMMAND ipc_client "conn" "tst1" "tst1" "tst1" "tst1" "tst1" "tst1" "tst1" "tst1")
add_test(NAME test_echos		COMMAND ipc_client "conn" "echo1" "1000" "echo2" "1000" "echo1" "1000" "echo2" "1000")
add_test(NAME exit_server		COMMAND ipc_client "conn" "exit")
add_test(NAME no_connection2	COMMAND ipc_client "noconn")


if(UNIX)
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "tingqian.li@intel.com")
endif()

if(WIN32)
set(CPACK_GENERATOR "NSIS")
endif()

include(CPack)
