include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(XZ_THREADS no)
set(XZ_NLS OFF)
set(XZ_DOC OFF)
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
FetchContent_Declare(vita3k_xz
    URL https://github.com/tukaani-project/xz/releases/download/v5.8.3/xz-5.8.3.tar.gz
    URL_HASH SHA256=3d3a1b973af218114f4f889bbaa2f4c037deaae0c8e815eec381c3d546b974a0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(vita3k_xz)

foreach(feature MBEDTLS NETTLE OPENSSL LIBB2 LZ4 LZO LZMA ZSTD LIBXML2 EXPAT
        WIN32_XMLLITE PCREPOSIX PCRE2POSIX CNG TAR CPIO CAT UNZIP XATTR ACL
        ICONV TEST COVERAGE INSTALL)
    set(ENABLE_${feature} OFF)
endforeach()
FetchContent_Declare(vita3k_libarchive
    URL https://github.com/libarchive/libarchive/releases/download/v3.8.9/libarchive-3.8.9.tar.gz
    URL_HASH SHA256=f5a6539059cf5e597dbeda37bfa4874b1e8dea063c8d93bf85a2b44af90a5bd4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(vita3k_libarchive)

# Use the bundled target instead of searching for a preinstalled liblzma.
target_compile_definitions(archive_static PRIVATE HAVE_LIBLZMA=1 HAVE_LZMA_H=1)
target_include_directories(archive_static PRIVATE "${vita3k_xz_SOURCE_DIR}/src/liblzma/api")
target_link_libraries(archive_static liblzma)
if(SWITCH)
    # Only streaming extraction is used; Horizon has no fchdir for the disk walker.
    get_target_property(archive_sources archive_static SOURCES)
    list(FILTER archive_sources EXCLUDE REGEX "archive_write.*\\.c$|archive_read_disk.*\\.c$|archive_read_extract2?\\.c$")
    set_property(TARGET archive_static PROPERTY SOURCES "${archive_sources}")
endif()
add_library(vita3k_7zip INTERFACE)
target_link_libraries(vita3k_7zip INTERFACE archive_static)
target_include_directories(vita3k_7zip INTERFACE "${vita3k_libarchive_SOURCE_DIR}")
target_compile_definitions(vita3k_7zip INTERFACE LIBARCHIVE_STATIC)
