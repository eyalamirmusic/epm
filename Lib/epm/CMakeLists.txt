add_library(epm-core STATIC
        Build/ChildBuild.h
        Build/Harvest.h
        Cache/BinaryCache.h
        Cache/CacheRoot.cpp
        Cache/CacheRoot.h
        Cache/SourceCache.h
        Emit/Response.h
        Spec/Env.h
        Spec/JsonFile.h
        Spec/Request.h
        Util/Git.cpp
        Util/Git.h
        Util/Sha256.cpp
        Util/Sha256.h)

target_include_directories(epm-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(epm-core PUBLIC eacp-core)

set_default_target_setting(epm-core)
