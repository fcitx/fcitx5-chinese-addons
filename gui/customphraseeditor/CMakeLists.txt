set(CUSTOMPHRASEEDITOR_SOURCES
  main.cpp
  customphraseeditor.cpp
  customphrasemodel.cpp
  editordialog.cpp
  ../../im/pinyin/customphrase.cpp
  )

add_library(customphraseeditor MODULE ${CUSTOMPHRASEEDITOR_SOURCES})
target_link_libraries(customphraseeditor Fcitx5Qt${QT_MAJOR_VERSION}::WidgetsAddons ${BROWSER_TARGET} Qt${QT_MAJOR_VERSION}::Concurrent LibIME::Core Boost::iostreams ${FMT_TARGET})
set_target_properties(customphraseeditor PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTOUIC_OPTIONS "-tr=fcitx::tr2fcitx;--include=fcitxqti18nhelper.h")

install(TARGETS customphraseeditor DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5/qt${QT_MAJOR_VERSION})
