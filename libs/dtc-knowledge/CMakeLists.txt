# dtc-knowledge: read-only lookup in the Isuzu knowledge database (Qt SQL / QSQLITE). Desktop only.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql)

add_library(dtc_knowledge STATIC
    include/knowledge/knowledge_repository.hpp
    src/knowledge_repository.cpp)
add_library(isuzu::dtc_knowledge ALIAS dtc_knowledge)
target_include_directories(dtc_knowledge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dtc_knowledge PUBLIC Qt6::Core Qt6::Sql isuzu::diagnostic_core)
target_compile_features(dtc_knowledge PUBLIC cxx_std_20)
isuzu_set_warnings(dtc_knowledge)
