add_executable(scel2org5 scel2org5.cpp)

target_link_libraries(scel2org5 Fcitx5::Utils)
install(TARGETS scel2org5 DESTINATION ${CMAKE_INSTALL_BINDIR})
