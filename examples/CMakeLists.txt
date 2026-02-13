file(GLOB ARC_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(example_src ${ARC_EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_src} NAME_WE)
    add_executable(arc_${example_name} ${example_src})

    target_link_libraries(arc_${example_name} PRIVATE arc asp fmt std23::nontype_functional)
    set_target_properties(arc_${example_name} PROPERTIES FOLDER "examples")

    if (ARC_FEATURE_ASAN)
        target_compile_options(arc_${example_name} PRIVATE -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer -g)
        target_link_options(arc_${example_name} PRIVATE -fsanitize=address -fsanitize=leak)
    endif()
endforeach()
