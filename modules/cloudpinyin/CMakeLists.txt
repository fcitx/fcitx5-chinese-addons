set(CLOUDPINYIN_SOURCES
    cloudpinyin.cpp
    fetch.cpp
)

if (ENABLE_CLOUDPINYIN)
add_library(cloudpinyin MODULE ${CLOUDPINYIN_SOURCES})
target_link_libraries(cloudpinyin Fcitx5::Core Fcitx5::Config PkgConfig::Curl Pthread::Pthread)
install(TARGETS cloudpinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
configure_file(cloudpinyin.conf.in.in cloudpinyin.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/cloudpinyin.conf.in" cloudpinyin.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cloudpinyin.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES ${CLOUDPINYIN_DATA_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/cloudpinyin")

fcitx5_export_module(CloudPinyin TARGET cloudpinyin BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS cloudpinyin_public.h INSTALL)
endif()

