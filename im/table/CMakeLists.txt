set(TABLE_SOURCES
    table.cpp
    ime.cpp
    context.cpp
)
add_library(table MODULE ${TABLE_SOURCES})
target_link_libraries(table Fcitx5::Core Fcitx5::Config LibIME::Table Boost::iostreams Fcitx5::Module::Punctuation Fcitx5::Module::Fullwidth Fcitx5::Module::QuickPhrase)
set_target_properties(table PROPERTIES PREFIX "")
install(TARGETS table DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES table.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
set(TABLE_NAMES db erbi qxm wanfeng wbpy wbx zrm cangjie)

foreach(TABLE ${TABLE_NAMES})
install(FILES inputmethod/${TABLE}.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
endforeach()
