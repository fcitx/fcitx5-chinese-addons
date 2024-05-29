set(TABLE_SOURCES
    engine.cpp
    state.cpp
    ime.cpp
    context.cpp
    candidate.cpp
)
add_library(table MODULE ${TABLE_SOURCES})
target_link_libraries(table Fcitx5::Core Fcitx5::Config LibIME::Table LibIME::Pinyin Boost::iostreams Fcitx5::Module::Punctuation Fcitx5::Module::QuickPhrase Fcitx5::Module::PinyinHelper LibIME::Pinyin ${FMT_TARGET})
target_compile_definitions(table PRIVATE FCITX_STRINGUTILS_ENABLE_BOOST_STRING_VIEW)
install(TARGETS table DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(table.conf.in.in table.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/table.conf.in" table.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/table.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
        COMPONENT config)

file(GLOB TABLE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.conf.in.in)
list(REMOVE_ITEM TABLE_FILES table.conf.in.in)
foreach(TABLE_ORIG ${TABLE_FILES})
  get_filename_component(TABLE_NAME "${TABLE_ORIG}" NAME_WE)
  configure_file(${TABLE_NAME}.conf.in.in "${CMAKE_CURRENT_BINARY_DIR}/${TABLE_NAME}.conf.in" @ONLY)
  fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/${TABLE_NAME}.conf.in" ${TABLE_NAME}.conf)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TABLE_NAME}.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
          COMPONENT config)
endforeach()
