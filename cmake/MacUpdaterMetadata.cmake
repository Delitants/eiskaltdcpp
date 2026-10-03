# Trust metadata only. Framework packaging and runtime activation are separate gates.
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
endif()
option(EISKALT_MAC_UPDATER_METADATA "Emit validated macOS updater trust metadata (does not enable updates)" OFF)
set(EISKALT_MAC_UPDATER_PUBLIC_KEY "" CACHE STRING "Explicit canonical base64 Ed25519 update public key")
set(EISKALT_MAC_UPDATER_FEED_URL "" CACHE STRING "Maintained flavor feed URL, optionally with a numeric revision query")
set(MACOSX_BUNDLE_UPDATER_METADATA "")
if(NOT EISKALT_MAC_UPDATER_METADATA)
    return()
endif()
if(NOT APPLE)
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_METADATA requires a macOS bundle")
endif()
if(CMAKE_VERSION VERSION_LESS "3.23")
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_METADATA requires CMake 3.23 or newer")
endif()

if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(_updater_feed_name "intel[.]xml")
elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(_updater_feed_name "apple-silicon[.]xml")
else()
    message(FATAL_ERROR "CMAKE_OSX_ARCHITECTURES must specify one updater release CPU: x86_64 or arm64")
endif()

# A 32-byte public key has 43 base64 data characters with canonical zero pad bits.
string(LENGTH "${EISKALT_MAC_UPDATER_PUBLIC_KEY}" _updater_key_length)
if(NOT _updater_key_length EQUAL 44 OR
   NOT EISKALT_MAC_UPDATER_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$" OR
   EISKALT_MAC_UPDATER_PUBLIC_KEY STREQUAL "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_PUBLIC_KEY must be an explicit canonical nonzero 32-byte base64 public key")
endif()
string(SUBSTRING "${EISKALT_MAC_UPDATER_PUBLIC_KEY}" 42 1 _updater_pad_character)
if(NOT _updater_pad_character MATCHES "^[AEIMQUYcgkosw048]$")
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_PUBLIC_KEY has noncanonical base64 pad bits")
endif()

# This agrees with the native URL policy and producer's flavor filenames.
string(LENGTH "${EISKALT_MAC_UPDATER_FEED_URL}" _updater_feed_length)
string(REGEX MATCH
    "^https://raw[.]githubusercontent[.]com/Delitants/eiskaltdcpp/codex-2[.]5[.]2-release/updates/${_updater_feed_name}([?]revision=[1-9][0-9]*)?$"
    _updater_validated_feed "${EISKALT_MAC_UPDATER_FEED_URL}")
if(_updater_feed_length GREATER 2048 OR _updater_validated_feed STREQUAL "" OR
   NOT _updater_validated_feed STREQUAL EISKALT_MAC_UPDATER_FEED_URL)
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_FEED_URL must be the maintained matching CPU feed, with only an optional canonical numeric revision query")
endif()

# The validated alphabets cannot inject XML. Qt, not Sparkle, owns scheduling and
# remote downloading; Sparkle receives only the verified app-owned local bridge.
set(MACOSX_BUNDLE_UPDATER_METADATA "
    <key>SUPublicEDKey</key><string>${EISKALT_MAC_UPDATER_PUBLIC_KEY}</string>
    <key>SUFeedURL</key><string>${EISKALT_MAC_UPDATER_FEED_URL}</string>
    <key>SURequireSignedFeed</key><true/>
    <key>SUVerifyUpdateBeforeExtraction</key><true/>
    <key>SUSignedFeedFailureExpirationInterval</key><integer>0</integer>
    <key>SUEnableAutomaticChecks</key><false/>
    <key>SUAutomaticallyUpdate</key><false/>
    <key>SUEnableSystemProfiling</key><false/>
    <key>SUEnableDownloaderService</key><false/>
    <key>SUEnableInstallerLauncherService</key><true/>
    <key>NSAppTransportSecurity</key>
    <dict><key>NSAllowsLocalNetworking</key><true/></dict>
")
