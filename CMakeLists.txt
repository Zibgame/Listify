cmake_minimum_required(VERSION 3.21)
project(Listify VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Test)

include(FetchContent)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v7.0.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_library(listify_core
    src/listifycore.cpp
)
target_include_directories(listify_core PUBLIC src)
target_link_libraries(listify_core PUBLIC Qt6::Core)

qt_add_executable(Listify
    src/main.cpp
    src/listifyapp.cpp
)
target_link_libraries(Listify PRIVATE listify_core Qt6::Core Qt6::Network
    ftxui::screen ftxui::dom ftxui::component)
if(WIN32)
    target_link_libraries(Listify PRIVATE shell32 user32)

    add_executable(ListifyLauncher WIN32 tools/root_launcher.cpp)
    target_link_libraries(ListifyLauncher PRIVATE user32)
    target_link_options(ListifyLauncher PRIVATE -static -municode)
    set_target_properties(ListifyLauncher PROPERTIES
        OUTPUT_NAME Listify
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

enable_testing()
qt_add_executable(ListifyTests tests/test_listify.cpp)
target_link_libraries(ListifyTests PRIVATE listify_core Qt6::Test)
add_test(NAME ListifyTests COMMAND ListifyTests)
