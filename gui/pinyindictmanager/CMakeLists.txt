add_library(pinyindictmanager MODULE
  main.cpp
  pinyindictmanager.cpp
  processrunner.cpp
  pipeline.cpp
  pipelinejob.cpp
  log.cpp
  renamefile.cpp
  filelistmodel.cpp
  browserdialog.cpp
  filedownloader.cpp
  )
target_link_libraries(pinyindictmanager Fcitx5Qt5::WidgetsAddons Fcitx5Qt5::DBusAddons Qt5::DBus Qt5::WebEngineWidgets Qt5::Concurrent)
set_target_properties(pinyindictmanager PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTOUIC_OPTIONS "-tr=fcitx::tr2fcitx;--include=fcitxqti18nhelper.h")

install(TARGETS pinyindictmanager DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5/qt5)
