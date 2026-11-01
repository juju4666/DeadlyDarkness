find_program(LINUXDEPLOY_EXECUTABLE
  NAMES linuxdeploy linuxdeploy-${CPACK_SYSTEM_PROCESSOR}.AppImage
  PATHS ${CPACK_PACKAGE_DIRECTORY}/linuxdeploy
)

set(linuxdeploy_prebuilt_archs x86_64 i386 aarch64 armhf)

if(NOT LINUXDEPLOY_EXECUTABLE)
  if(NOT CPACK_SYSTEM_PROCESSOR IN_LIST linuxdeploy_prebuilt_archs)
    message(FATAL_ERROR "linuxdeploy could not be found, and is not available to download for your system.")
  endif()

  message(STATUS "Downloading linuxdeploy")
  set(LINUXDEPLOY_EXECUTABLE ${CPACK_PACKAGE_DIRECTORY}/linuxdeploy/linuxdeploy)
  file(DOWNLOAD
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${CPACK_SYSTEM_PROCESSOR}.AppImage"
    ${LINUXDEPLOY_EXECUTABLE}
    INACTIVITY_TIMEOUT 10
  )

  execute_process(
    COMMAND chmod +x ${LINUXDEPLOY_EXECUTABLE}
    COMMAND_ECHO STDOUT
  )
endif()

# appimagetool needs <Icon>.png at the top of the AppDir (it makes .DirIcon from it); an SVG alone is refused
file(COPY_FILE
  "${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps/${CPACK_BIN_NAME}.png"
  "${CPACK_TEMPORARY_DIRECTORY}/${CPACK_BIN_NAME}.png"
)

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
    LDAI_OUTPUT=${CPACK_PACKAGE_FILE_NAME}.appimage
    LINUXDEPLOY_OUTPUT_VERSION=${CPACK_PACKAGE_VERSION}
    NO_STRIP=1
    ${LINUXDEPLOY_EXECUTABLE}
    --appimage-extract-and-run
    --appdir=${CPACK_TEMPORARY_DIRECTORY}
    --executable=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/${CPACK_BIN_DIR}/${CPACK_BIN_NAME}
    --desktop-file=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/applications/${CPACK_BIN_NAME}.desktop
    --icon-file=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps/${CPACK_BIN_NAME}.svg
    --icon-file=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps/${CPACK_BIN_NAME}.png
    --output=appimage
  RESULT_VARIABLE linuxdeploy_result
)

if(NOT linuxdeploy_result EQUAL 0)
  message(FATAL_ERROR "linuxdeploy failed (${linuxdeploy_result}): no AppImage was made")
endif()

# linuxdeploy writes the AppImage into the directory it was run from; CI picks up build/*.appimage
file(GLOB appimages "${CPACK_PACKAGE_DIRECTORY}/*.appimage" "${CMAKE_CURRENT_BINARY_DIR}/*.appimage" "./*.appimage")
list(LENGTH appimages num_appimages)
if(num_appimages EQUAL 0)
  message(FATAL_ERROR "linuxdeploy ran but no .appimage file was found")
endif()
foreach(img ${appimages})
  get_filename_component(img_dir "${img}" DIRECTORY)
  get_filename_component(want_dir "${CPACK_PACKAGE_DIRECTORY}" ABSOLUTE)
  get_filename_component(img_abs_dir "${img_dir}" ABSOLUTE)
  if(NOT img_abs_dir STREQUAL want_dir)
    file(COPY "${img}" DESTINATION "${CPACK_PACKAGE_DIRECTORY}")
  endif()
endforeach()
