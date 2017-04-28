set(PUNCTUATION_SOURCES
    punctuation.cpp
)

set(PUNCTUATION_DATA_FILES
  data/punc.mb.zh_CN
  data/punc.mb.zh_HK
  data/punc.mb.zh_TW
)

add_library(punctuation MODULE ${PUNCTUATION_SOURCES})
target_link_libraries(punctuation Fcitx5::Core Fcitx5::Config Fcitx5::Module::Notifications)
set_target_properties(punctuation PROPERTIES PREFIX "")
install(TARGETS punctuation DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES punctuation.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES ${PUNCTUATION_DATA_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/punctuation")
