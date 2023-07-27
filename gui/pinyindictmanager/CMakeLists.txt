set(DICTMANAGER_SOURCES
  main.cpp
  pinyindictmanager.cpp
  processrunner.cpp
  pipeline.cpp
  pipelinejob.cpp
  log.cpp
  renamefile.cpp
  filelistmodel.cpp
  )

if (ENABLE_BROWSER)
    list(APPEND DICTMANAGER_SOURCES browserdialog.cpp filedownloader.cpp)

    if (TARGET Qt5::WebKit)
        set(BROWSER_TARGET Qt5::WebKitWidgets)
    else()
        set(BROWSER_TARGET Qt5::WebEngineWidgets)
    endif()
endif()

add_library(pinyindictmanager MODULE ${DICTMANAGER_SOURCES})
target_link_libraries(pinyindictmanager Fcitx5Qt5::WidgetsAddons ${BROWSER_TARGET} Qt5::Concurrent)
set_target_properties(pinyindictmanager PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTOUIC_OPTIONS "-tr=fcitx::tr2fcitx;--include=fcitxqti18nhelper.h")

install(TARGETS pinyindictmanager DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5/qt5)
