set(TABLE_SOURCES
    table.cpp
    ime.cpp
    context.cpp
)
add_library(table MODULE ${TABLE_SOURCES})
target_link_libraries(table Fcitx5::Core Fcitx5::Config LibIME::Table Boost::iostreams Fcitx5::Module::Punctuation Fcitx5::Module::Fullwidth Fcitx5::Module::QuickPhrase)
set_target_properties(table PROPERTIES PREFIX "")
install(TARGETS table DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(table.conf.in table.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/table.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

file(GLOB TABLE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.conf.in)
# remove the addon config
list(REMOVE_ITEM TABLE_FILES "table.conf.in")
foreach(TABLE_ORIG ${TABLE_FILES})
string(REPLACE ".in" "" TABLE_FILE ${TABLE_ORIG})
fcitx5_translate_desktop_file(${TABLE_ORIG} ${TABLE_FILE})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TABLE_FILE}" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
endforeach()
