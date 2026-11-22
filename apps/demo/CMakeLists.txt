# /api/demo -- business app DLL fut_app_demo.dll (deploy updates by swapping this file)
#
# Declaration/definition split: the umbrella header is the ONE header the
# generated export unit includes and scans (it pulls in the per-feature
# declarations), and every .cpp below is compiled normally as a definition unit
# and linked into the same DLL.
#
# Adding a controller: one #include in demo.hpp + one .cpp here.
add_app(demo /api/demo
    demo.hpp
    services.cpp
    middleware.cpp
    routes.cpp
    routes/core.cpp
    routes/binding.cpp
    routes/runtime.cpp
    routes/items.cpp
    routes/nested.cpp
)
