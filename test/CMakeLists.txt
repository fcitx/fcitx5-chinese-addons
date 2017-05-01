add_executable(testpuncutation testpuncutation.cpp)
target_link_libraries(testpuncutation Fcitx5::Core Fcitx5::Module::Punctuation)
add_dependencies(testpuncutation punctuation)
add_test(NAME testpuncutation COMMAND testpuncutation ${PROJECT_SOURCE_DIR}/modules/punctuation ${PROJECT_BINARY_DIR}/modules/punctuation)
