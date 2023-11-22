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

    if (TARGET Qt${QT_MAJOR_VERSION}::WebKit)
        set(BROWSER_TARGET Qt${QT_MAJOR_VERSION}::WebKitWidgets)
    else()
        set(BROWSER_TARGET Qt${QT_MAJOR_VERSION}::WebEngineWidgets)
    endif()
endif()

add_library(pinyindictmanager MODULE ${DICTMANAGER_SOURCES})
target_link_libraries(pinyindictmanager Fcitx5Qt${QT_MAJOR_VERSION}::WidgetsAddons ${BROWSER_TARGET} Qt${QT_MAJOR_VERSION}::Concurrent)
set_target_properties(pinyindictmanager PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTOUIC_OPTIONS "-tr=fcitx::tr2fcitx;--include=fcitxqti18nhelper.h")

install(TARGETS pinyindictmanager DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5/qt${QT_MAJOR_VERSION})
