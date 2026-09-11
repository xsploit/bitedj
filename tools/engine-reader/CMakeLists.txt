cmake_minimum_required(VERSION 3.24)
project(BiteDJEngineReader LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Git REQUIRED)
include(FetchContent)
set(BITEDJ_ENGINE_ARCHIVE "https://codeload.github.com/xsco/libdjinterop/tar.gz/99b5130a70232cfae1666dd268f7b9a05bbaaaa6" CACHE STRING "Pinned source archive URL or local archive path")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SYSTEM_SQLITE ON CACHE BOOL "" FORCE)
FetchContent_Declare(engine_djinterop
  URL "${BITEDJ_ENGINE_ARCHIVE}"
  URL_HASH SHA256=61c89760569b8ac87df0b8f19d8e70f195e94d47622f196c27c51e993423cd13
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  PATCH_COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/libdjinterop-native115.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/libdjinterop-bounded-decompression.patch")
FetchContent_MakeAvailable(engine_djinterop)
add_executable(engine-reader engine-reader.cpp)
target_link_libraries(engine-reader PRIVATE DjInterop Qt6::Core)
set_target_properties(engine-reader PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS engine-reader DjInterop RUNTIME DESTINATION libexec/bitedj-engine LIBRARY DESTINATION libexec/bitedj-engine)
install(PROGRAMS engine-import.py DESTINATION libexec/bitedj-engine)
install(FILES engine_limits.py engine_import_package.py engine_playlist_manifest.py engine_reader_protocol.py engine_media_path.py DESTINATION libexec/bitedj-engine)
install(FILES "${engine_djinterop_SOURCE_DIR}/LICENSE" DESTINATION share/licenses/bitedj-engine-reader RENAME libdjinterop-LICENSE)
install(FILES README.md DESTINATION share/doc/bitedj-engine-reader)
