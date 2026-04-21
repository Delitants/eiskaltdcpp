find_path(MINIUPNP_INCLUDE_DIR miniupnpc/miniupnpc.h
   HINTS
     ENV DEPS_PREFIX
     /opt/homebrew/opt/miniupnpc
   PATH_SUFFIXES opt/miniupnpc/include include)
find_library(MINIUPNP_LIBRARY miniupnpc
   HINTS
     ENV DEPS_PREFIX
     /opt/homebrew/opt/miniupnpc
   PATH_SUFFIXES opt/miniupnpc/lib lib)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Miniupnpc DEFAULT_MSG MINIUPNP_LIBRARY MINIUPNP_INCLUDE_DIR)

MARK_AS_ADVANCED(
  MINIUPNP_INCLUDE_DIR
  MINIUPNP_LIBRARY
)
