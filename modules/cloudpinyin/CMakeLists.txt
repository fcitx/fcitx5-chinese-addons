set(CLOUDPINYIN_SOURCES
    cloudpinyin.cpp
    fetch.cpp
)

add_library(cloudpinyin MODULE ${CLOUDPINYIN_SOURCES})
target_link_libraries(cloudpinyin Fcitx5::Core Fcitx5::Config PkgConfig::Curl Pthread::Pthread)
set_target_properties(cloudpinyin PROPERTIES PREFIX "")
install(TARGETS cloudpinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES cloudpinyin.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES ${CLOUDPINYIN_DATA_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/cloudpinyin")

fcitx5_export_module(CloudPinyin TARGET cloudpinyin BUILD_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS cloudpinyin_public.h INSTALL)

