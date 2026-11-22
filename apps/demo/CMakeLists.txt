# /api/demo -- business app DLL fut_app_demo.dll (deploy updates by swapping this file)
#
# Only the umbrella header is listed: the generated export unit includes it and
# scans the 'demo' namespace, which includes the per-feature modules in a fixed
# order. Adding a controller means adding one header to demo.hpp -- the build
# file does not change.
add_app(demo /api/demo demo.hpp)
