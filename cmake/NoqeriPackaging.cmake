# Portable Noqeri SDK -> native installable packages with CPack.
# Used by BOTH full builds and NOQERI_GRAPHICS_PORTABLE_BUILD.
# Each release asset names its host ABI, not the compiler's codegen targets.
set(CPACK_PACKAGE_NAME "noqeri")
set(CPACK_PACKAGE_VENDOR "Noethric")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Noqeri compiler, NQRI virtual machine and source SDK")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/EMN90909/Noqeri")
set(CPACK_PACKAGE_CONTACT "Noethric contributors")
# Apple productbuild requires license resources to have a recognized suffix
# (.txt/.rtf/.html), while the repository's canonical file is LICENSE.
configure_file("${CMAKE_SOURCE_DIR}/LICENSE"
 "${CMAKE_CURRENT_BINARY_DIR}/NoqeriLicense.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/NoqeriLicense.txt")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Noqeri")
set(CPACK_VERBATIM_VARIABLES ON)

if(NOT NOQERI_PACKAGE_TARGET)
  # Local source builds have no matrix-provided artifact identity.
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _noqeri_system)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _noqeri_cpu)
  set(NOQERI_PACKAGE_TARGET "${_noqeri_system}-${_noqeri_cpu}")
endif()
if(NOT NOQERI_PACKAGE_TARGET MATCHES "^[a-zA-Z0-9_-]+$")
  message(FATAL_ERROR "Invalid NOQERI_PACKAGE_TARGET (must be an OS-CPU identifier)")
endif()
set(CPACK_PACKAGE_FILE_NAME "noqeri-${PROJECT_VERSION}-${NOQERI_PACKAGE_TARGET}")

# Native Unix installs must go to system-managed locations. Using a relative
# install() DESTINATION allows --prefix / custom staging and CPack packaging.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Noethric contributors")
  set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/EMN90909/Noqeri")
  set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
  set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
  set(CPACK_RPM_PACKAGE_URL "https://github.com/EMN90909/Noqeri")
  set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
  if(NOQERI_PACKAGE_TARGET MATCHES "linux-x86$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "i686")
  elseif(NOQERI_PACKAGE_TARGET MATCHES "linux-x86_64$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
  elseif(NOQERI_PACKAGE_TARGET MATCHES "linux-aarch64$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
  set(CPACK_FREEBSD_PACKAGE_ORIGIN "devel/noqeri")
  set(CPACK_FREEBSD_PACKAGE_MAINTAINER "Noethric contributors")
  set(CPACK_FREEBSD_PACKAGE_LICENSE "GPLv3")
elseif(APPLE)
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
  set(CPACK_PRODUCTBUILD_IDENTIFIER "xyz.noethric.noqeri")
elseif(WIN32)
  # ZIP and NSIS can package x86/x64/ARM64 CLI payloads. WiX supports
  # x86/x64 natively and ARM64 with a new enough toolset.
  set(CPACK_NSIS_DISPLAY_NAME "Noqeri ${PROJECT_VERSION}")
  set(CPACK_NSIS_PACKAGE_NAME "Noqeri")
  set(CPACK_NSIS_MODIFY_PATH ON)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_WIX_VERSION "3")
  # WiX requires an RTF license; the generic CPack LICENSE is plain text.
  # This is the complete GPL-3.0 text, converted to RTF for installer UI.
  set(CPACK_WIX_LICENSE_RTF "${CMAKE_SOURCE_DIR}/cmake/NoqeriLicense.rtf")
  if(NOQERI_PACKAGE_TARGET MATCHES "windows-x86$")
    set(CPACK_WIX_ARCHITECTURE "x86")
    set(CPACK_WIX_UPGRADE_GUID "e7f99213-cb9a-4d25-b36c-b027ff11d024")
  elseif(NOQERI_PACKAGE_TARGET MATCHES "windows-aarch64$")
    set(CPACK_WIX_ARCHITECTURE "arm64")
    set(CPACK_WIX_UPGRADE_GUID "bc0c95da-7f45-4662-aec8-2415cf351ad4")
  else()
    set(CPACK_WIX_ARCHITECTURE "x64")
    set(CPACK_WIX_UPGRADE_GUID "9ed3d1c1-49d8-4d1f-a385-f5b3308252fd")
  endif()
endif()

# Archive generation is available everywhere without external packagers.
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
