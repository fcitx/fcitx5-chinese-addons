add_custom_target(copy-im DEPENDS pinyin.conf.in-fmt erbi.conf.in-fmt)
add_custom_command(TARGET copy-im COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/im/pinyin/pinyin.conf ${CMAKE_CURRENT_BINARY_DIR}/pinyin.conf)
add_custom_command(TARGET copy-im COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/im/table/erbi.conf ${CMAKE_CURRENT_BINARY_DIR}/erbi.conf)

add_custom_target(copy-testim DEPENDS pinyin.conf.in-fmt)
add_custom_command(TARGET copy-testim COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/test/inputmethod/sim.conf ${PROJECT_BINARY_DIR}/test/inputmethod/sim.conf)
add_custom_command(TARGET copy-testim COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/test/inputmethod/trad.conf ${PROJECT_BINARY_DIR}/test/inputmethod/trad.conf)
