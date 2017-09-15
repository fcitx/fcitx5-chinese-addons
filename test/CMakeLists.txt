add_executable(testpuncutation testpuncutation.cpp)
target_link_libraries(testpuncutation Fcitx5::Core Fcitx5::Module::Punctuation)
add_dependencies(testpuncutation punctuation punctuation.conf.in-fmt)
add_test(NAME testpuncutation COMMAND testpuncutation ${PROJECT_BINARY_DIR}/modules/punctuation)

add_executable(testcloudpinyin testcloudpinyin.cpp)
target_link_libraries(testcloudpinyin Fcitx5::Core Fcitx5::Module::CloudPinyin)
add_dependencies(testcloudpinyin cloudpinyin)
