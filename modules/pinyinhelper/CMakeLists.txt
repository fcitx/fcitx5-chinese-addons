set(PINYINHELPER_SOURCES
    pinyinhelper.cpp
    pinyinlookup.cpp
    stroke.cpp
)
add_library(pinyinhelper MODULE ${PINYINHELPER_SOURCES})
target_link_libraries(pinyinhelper Fcitx5::Core Fcitx5::Config LibIME::Core Boost::iostreams Fcitx5::Module::QuickPhrase Fcitx5::Module::Clipboard ${FMT_TARGET})
install(TARGETS pinyinhelper DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(pinyinhelper.conf.in.in pinyinhelper.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/pinyinhelper.conf.in" pinyinhelper.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pinyinhelper.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        COMPONENT config)

fcitx5_export_module(PinyinHelper TARGET pinyinhelper BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS pinyinhelper_public.h INSTALL)

set(PY_STROKE_VER 20121124)
set(PY_STROKE_TGT "${CMAKE_CURRENT_BINARY_DIR}/py_stroke.mb")
set(PY_STROKE_TAR "py_stroke-${PY_STROKE_VER}.tar.gz")
set(PY_STROKE_URL
  "http://download.fcitx-im.org/data/py_stroke-${PY_STROKE_VER}.tar.gz")
fcitx5_download(py-stroke-download "${PY_STROKE_URL}" "${PY_STROKE_TAR}"
  8eb128a9bfa43952e67cf2fcee1fd134c6f4cfd317bc2f6c38a615f5eb64e248)
fcitx5_extract(py-stroke-extract "${PY_STROKE_TAR}" DEPENDS py-stroke-download
  OUTPUT ${PY_STROKE_TGT})
install(FILES "${PY_STROKE_TGT}" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/pinyinhelper")

set(PY_TABLE_VER 20121124)
set(PY_TABLE_TGT "${CMAKE_CURRENT_BINARY_DIR}/py_table.mb")
set(PY_TABLE_TAR "py_table-${PY_TABLE_VER}.tar.gz")
set(PY_TABLE_URL
  "http://download.fcitx-im.org/data/py_table-${PY_TABLE_VER}.tar.gz")
fcitx5_download(py-table-download "${PY_TABLE_URL}" "${PY_TABLE_TAR}"
  42146ac97de6c13d55f9e99ed873915f4c66739e9c11532a34556badf9792c04)
fcitx5_extract(py-table-extract "${PY_TABLE_TAR}" DEPENDS py-table-download
  OUTPUT ${PY_TABLE_TGT})
install(FILES "${PY_TABLE_TGT}" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/pinyinhelper")
