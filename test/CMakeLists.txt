

add_executable(ipc_client ipc_client.cpp)
add_executable(ipc_server ipc_server.cpp)

# add_dependencies(test-ipc ${TARGET_LIB})

target_link_libraries(ipc_client PRIVATE ${TARGET_LIB})
target_link_libraries(ipc_server PRIVATE ${TARGET_LIB})

install (TARGETS ipc_client ipc_server
         RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/bin
         LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${TARGET_LIB}/bin)
