set(CUSTOMPHRASEEDITOR_SOURCES
  main.cpp
  customphraseeditor.cpp
  customphrasemodel.cpp
  editordialog.cpp
  ../../im/pinyin/customphrase.cpp
  )

add_library(customphraseeditor MODULE ${CUSTOMPHRASEEDITOR_SOURCES})
target_link_libraries(customphraseeditor Fcitx5Qt5::WidgetsAddons ${BROWSER_TARGET} Qt5::Concurrent LibIME::Core Boost::iostreams ${FMT_TARGET})
set_target_properties(customphraseeditor PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTOUIC_OPTIONS "-tr=fcitx::tr2fcitx;--include=fcitxqti18nhelper.h")

install(TARGETS customphraseeditor DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5/qt5)
