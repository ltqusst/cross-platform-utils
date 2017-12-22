

add_executable(test-ipc test-ipc.cpp)
add_dependencies(test-ipc ${TARGET_LIB})
target_link_libraries(test-ipc ${TARGET_LIB})

install (TARGETS test-ipc
         RUNTIME DESTINATION ${PROJECT_SOURCE_DIR}/_install
         LIBRARY DESTINATION ${PROJECT_SOURCE_DIR}/_install)

