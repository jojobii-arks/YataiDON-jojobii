# Clear only generated asset trees so removed source assets do not ship in
# incremental builds. The app's writable Documents directory is unrelated.
foreach(_required SOURCE_DIR SKINS_DIR SONGS_DIR DEST_DIR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "ios_assets.cmake requires -D${_required}=<path>")
  endif()
  if(NOT IS_ABSOLUTE "${${_required}}")
    message(FATAL_ERROR "${_required} must be an absolute path (got '${${_required}}')")
  endif()
endforeach()
file(REMOVE_RECURSE "${DEST_DIR}/Skins" "${DEST_DIR}/Songs" "${DEST_DIR}/shader")
file(MAKE_DIRECTORY "${DEST_DIR}")
file(COPY "${SOURCE_DIR}/shader" DESTINATION "${DEST_DIR}")
file(COPY "${SKINS_DIR}/" DESTINATION "${DEST_DIR}/Skins"
  PATTERN ".git" EXCLUDE PATTERN ".git*" EXCLUDE)
file(COPY "${SONGS_DIR}/" DESTINATION "${DEST_DIR}/Songs" PATTERN ".git*" EXCLUDE)
