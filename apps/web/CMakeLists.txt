# /api/web -- business app DLL fut_app_web.dll (deploy updates by swapping this file)
# Declaration/definition split: the header is scanned, the cpp is compiled
# normally and linked into the same DLL.
add_app(web /api/web web_routes.hpp web_routes.cpp)
