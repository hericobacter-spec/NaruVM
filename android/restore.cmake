# Portable counterparts of tools/build_ios_restore.sh. One crypto configuration
# is shared by the virtual AES device, libimobiledevice and libcurl.
set(TP "${ROOT}/third_party")
set(CURL_SOURCE "${ROOT}/.tools/curl-8.23.0")
if(NOT EXISTS "${CURL_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Run python3 android/fetch_restore_deps.py first")
endif()
add_compile_definitions(MBEDTLS_CONFIG_FILE="henge_restore_mbedtls_config.h")
file(GLOB TLS_SOURCES "${TP}/mbedtls/library/*.c")
add_library(henge_tls STATIC ${TLS_SOURCES})
target_include_directories(henge_tls PUBLIC "${TP}/mbedtls/include")
target_compile_options(henge_tls PRIVATE -w)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_USE_CMAKECONFIG OFF CACHE BOOL "" FORCE)
set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(CURL_CA_PATH "/apex/com.android.conscrypt/cacerts" CACHE STRING "" FORCE)
set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
set(MBEDTLS_INCLUDE_DIR "${TP}/mbedtls/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY henge_tls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY henge_tls CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY henge_tls CACHE STRING "" FORCE)
add_subdirectory("${CURL_SOURCE}" "${CMAKE_BINARY_DIR}/curl" EXCLUDE_FROM_ALL)

foreach(OPT COMMONCRYPTO GNUTLS MBEDTLS OPENSSL WINDOWS_CRYPTO BZIP2 LZMA ZSTD)
    set(ENABLE_${OPT} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(OPT TOOLS REGRESS OSSFUZZ EXAMPLES DOC)
    set(BUILD_${OPT} OFF CACHE BOOL "" FORCE)
endforeach()
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/libzip" "${CMAKE_BINARY_DIR}/libzip" EXCLUDE_FROM_ALL)

file(GLOB RESTORE_SOURCES
    "${TP}/libplist/src/*.c" "${TP}/libplist/libcnary/*.c"
    "${TP}/libimobiledevice-glue/src/*.c" "${TP}/libusbmuxd/src/*.c"
    "${TP}/libimobiledevice/src/*.c" "${TP}/libimobiledevice/common/*.c"
    "${TP}/libtatsu/src/*.c" "${TP}/usbmuxd/src/*.c" "${TP}/idevicerestore/src/*.c")
file(GLOB MUX_SOURCES "${TP}/usbmuxd/src/*.c")
set_source_files_properties(${MUX_SOURCES} PROPERTIES COMPILE_DEFINITIONS "main=usbmuxd_main;log_level=usbmuxd_log_level")
add_library(henge_restore STATIC ${RESTORE_SOURCES} "${ROOT}/host/install.c")
target_compile_definitions(henge_restore PRIVATE HAVE_CONFIG_H HAVE_MBEDTLS IDEVICERESTORE_NOMAIN
    PACKAGE_NAME="Henge-Android" PACKAGE_VERSION="1.0.0-henge-android" PACKAGE_STRING="Henge-Android")
target_compile_options(henge_restore PRIVATE -w)
target_include_directories(henge_restore PRIVATE "${ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/native/restore_config"
    "${TP}" "${TP}/libusb-1.0" "${TP}/libplist/include" "${TP}/libplist/src" "${TP}/libplist/libcnary/include"
    "${TP}/libimobiledevice-glue/include" "${TP}/libusbmuxd/include"
    "${TP}/libimobiledevice/include" "${TP}/libimobiledevice" "${TP}/libimobiledevice/src" "${TP}/libimobiledevice/common"
    "${TP}/libtatsu/include" "${TP}/libirecovery/include" "${TP}/idevicerestore/src"
    "${TP}/libzip/lib" "${CMAKE_BINARY_DIR}/libzip")
target_link_libraries(henge_restore PUBLIC henge_recovery henge_tls CURL::libcurl libzip::zip z)
