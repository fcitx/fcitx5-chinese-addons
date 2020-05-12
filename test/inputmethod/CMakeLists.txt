add_custom_target(copy-im DEPENDS pinyin.conf.in-fmt)
add_custom_command(TARGET copy-im COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/im/pinyin/pinyin.conf ${CMAKE_CURRENT_BINARY_DIR}/pinyin.conf)


