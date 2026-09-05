cmake_minimum_required(VERSION 3.25)

# This tells cmake we have goodies in the /cmake folder
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include (PamplejuceVersion)

# Modern concise way to add dependencies to your project
include (CPM)

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# =============================================================================
# NEW PLUGIN? Of everything set below, these are the five that matter:
#   PROJECT_NAME   internal, no spaces
#   PRODUCT_NAME   what DAWs display, spaces allowed
#   BUNDLE_ID      reverse-DNS, must be unique per plugin
#   PLUGIN_CODE    four characters, unique per plugin, at least one uppercase.
#                  Two plugins sharing one are the same plugin as far as a host
#                  is concerned, and the second to load simply does not.
#   LV2URI         stable for the life of the plugin; changing it orphans every
#                  session that referenced the old one
# =============================================================================

# Change me!
# This is the internal name of the project and the name of JUCE's shared code target
# Note: This cannot have spaces (it may be 2024, but you can't have it all!)
# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs display)
set(PROJECT_NAME "SPACE")

# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs will display)
# You can also just have it be the same thing as PROJECT_NAME
# You may want to append the major version on the end of this (and PROJECT_NAME) ala:
#   set(PROJECT_NAME "MyPlugin_v${MAJOR_VERSION}")
# Doing so enables major versions to show up in IDEs and DAWs as separate plugins
# allowing you to change parameters and behavior without breaking existing user projects
set(PRODUCT_NAME "SPACE")

# Change me! Used for the MacOS bundle name and Installers
set(COMPANY_NAME "Celine Audio")

# Change me! Used for the MacOS bundle identifier (and signing)
set(BUNDLE_ID "com.celineaudio.space")

# Change me! Whether a standalone application is built alongside the plug-in
# formats, and whether it asks for microphone access.
#
# The two go together more than they look like they do. macOS will not give an app
# audio *input* without NSMicrophoneUsageDescription in its Info.plist -- and it does
# not warn: there is no prompt, the device opens, and every input sample arrives as
# silence. MICROPHONE_PERMISSION_ENABLED below is what puts that key there.
#
# So a standalone that processes live input needs both. One that only plays files
# needs neither, and asking for a permission you never use is a thing people notice
# and hold against you.
set(BUILD_STANDALONE FALSE)
set(NEEDS_MICROPHONE FALSE)

# ASIO gives a Windows standalone low-latency input and output, and is the only way
# most Windows users get either. It needs Steinberg's SDK, which is not vendored here
# and cannot be: the licence is yours to accept, not this template's.
#
# Detected rather than assumed. Setting JUCE_ASIO=1 without the SDK does not degrade
# gracefully -- juce_audio_devices includes <iasiodrv.h> and the Windows build simply
# fails to compile. So the flag goes on only when the header is actually there, which
# means everything builds for everyone and gains ASIO for whoever has it.
#
# Download the SDK from Steinberg, unpack it, and either set ASIO_SDK_DIR in your
# environment or pass -DASIO_SDK_DIR=/path/to/asiosdk to CMake.
set(WANT_ASIO FALSE)
set(ASIO_SDK_DIR "$ENV{ASIO_SDK_DIR}" CACHE PATH "Unpacked Steinberg ASIO SDK")

set(HAVE_ASIO FALSE)

if (WANT_ASIO AND BUILD_STANDALONE AND WIN32)
    if (ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        set(HAVE_ASIO TRUE)
        message(STATUS "ASIO: enabled, SDK at ${ASIO_SDK_DIR}")
    else ()
        message(STATUS "ASIO: wanted but no SDK found -- set ASIO_SDK_DIR to enable. Building without it.")
    endif ()
endif ()

# Change me! Set the plugin formats you want built
# Valid choices: AAX Unity VST VST3 AU AUv3 LV2 Standalone
set(FORMATS AAX AU VST3 LV2)

if (BUILD_STANDALONE)
    list(PREPEND FORMATS Standalone)
endif ()

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

# For simplicity, the name of the CMake project is also the name of the target
project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# Log build environment info (after project() so compiler info is available)
include(PamplejuceLog)

# JUCE is setup as a submodule in the /JUCE folder
# Locally, you must run `git submodule update --init --recursive` once
# and later `git submodule update --remote --merge` to keep it up to date
# On Github Actions, this is done as a part of actions/checkout
add_subdirectory(JUCE)

# Add CLAP format
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add any other modules you want modules here, before the juce_add_plugin call
# juce_add_module(modules/my_module)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Bundle icon, shown wherever a host or the Finder displays the plug-in
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    # Change me!
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"

    # On MacOS, plugin is copied to /Users/yourname/Library/Audio/Plug-Ins/
    COPY_PLUGIN_AFTER_BUILD FALSE

    # Change me!
    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE Celi

    # Change me!
    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE SPAC
    LV2URI "https://github.com/Celine-audio/SPACE"
    FORMATS "${FORMATS}"

    # Only meaningful for the standalone build; a plug-in gets its input from the
    # host, which has already asked on its own behalf. The text is what macOS shows
    # in the permission dialog, so it should say why rather than that.
    MICROPHONE_PERMISSION_ENABLED ${NEEDS_MICROPHONE}
    MICROPHONE_PERMISSION_TEXT "${PRODUCT_NAME} needs microphone access to process live audio input."

    # The name of your final executable
    # This is how it's listed in the DAW
    # This can be different from PROJECT_NAME and can have spaces!
    # You might want to use v${MAJOR_VERSION} here once you go to v2...
    PRODUCT_NAME "${PRODUCT_NAME}")

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES audio-effect)

# Enable fast math, C++20 and a few other target defaults
include(SharedCodeDefaults)


# Manually list all .h and .cpp files for the plugin
# If you are like me, you'll use globs for your sanity.
# Just ensure you employ CONFIGURE_DEPENDS so the build system picks up changes
# If you want to appease the CMake gods and avoid globs, manually add files like so:
# set(SourceFiles Source/PluginEditor.h Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginProcessor.cpp)
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Embeds assets/ as BinaryData. The only things in there are the format marks the
# About window shows; the interface itself is drawn in code and uses the system font.
include(Assets)

# MacOS only: Cleans up folder and target organization on Xcode.
include(XcodePrettify)

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    # JUCE_WEB_BROWSER and JUCE_USE_CURL off by default
    JUCE_WEB_BROWSER=0  # If you set this to 1, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Uncomment if you are paying for a an Indie/Pro license or releasing under GPLv3
    # JUCE_DISPLAY_SPLASH_SCREEN=0

    # Windows standalone only. See the ASIO block above for why this is conditional
    # rather than simply switched on.
    $<$<BOOL:${HAVE_ASIO}>:JUCE_ASIO=1>

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    # JucePlugin_Name doesn't use the nicer PRODUCT_NAME, so it is passed through
    # separately. Derived rather than written out, so it cannot be left naming the
    # previous plugin after a rename.
    PRODUCT_NAME_WITHOUT_VERSION="${PRODUCT_NAME}"
)

# Link to any other modules you added (with juce_add_module) here!
# Usually JUCE modules must have PRIVATE visibility
# See https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md#juce_add_module
# However, with Pamplejuce, you'll link modules to SharedCode with INTERFACE visibility
# This allows the JUCE plugin targets and the Tests target to link against it
target_link_libraries(SharedCode
    INTERFACE
    Assets
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if (HAVE_ASIO)
    target_include_directories(SharedCode INTERFACE "${ASIO_SDK_DIR}/common")
endif ()

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# IPP support, comment out to disable
include(PamplejuceIPP)

# Everything related to the tests target
include(Tests)

# A separate target for Benchmarks (keeps the Tests target fast)
include(Benchmarks)

# The Benchmarks module globs only Catch2Main.cpp, where Tests.cmake globs every .cpp,
# so any other benchmark file has to be named here or it is silently not compiled. The
# module lives in the shared cmake submodule and is not ours to change; naming the
# sources is the part that belongs to this project anyway.
target_sources(Benchmarks PRIVATE benchmarks/Benchmarks.cpp)

# PamplejuceVersion only splits out MAJOR_VERSION unless the auto-bump option is on,
# so MINOR_VERSION and PATCH_LEVEL reach the CI env file empty. Nothing uses them
# today, which is exactly why it would go unnoticed: the first workflow to reach for
# ${{ env.PATCH_LEVEL }} would silently get an empty string. Filled in here rather
# than in the module, which lives in the shared cmake submodule and is not ours.
if (NOT DEFINED MINOR_VERSION OR MINOR_VERSION STREQUAL "")
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _version_match "${CURRENT_VERSION}")
    set(MINOR_VERSION "${CMAKE_MATCH_2}")

    if (CMAKE_MATCH_4)
        set(PATCH_LEVEL "${CMAKE_MATCH_4}")
    else ()
        set(PATCH_LEVEL "0")
    endif ()
endif ()

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
