# The component targets that belong to this repository.
#
# ESP-IDF components live under `IDF_PATH` and vendored code lives under a
# `third_party/` directory. Everything else is ours: the components in
# `components/`, each subproject's `main`, and the HAL implementations.
#
# The compiler-warning and GCC-analyzer modules both add flags to exactly that
# set, so they share this filter instead of each carrying a copy. Call it after
# `project()`: both `BUILD_COMPONENTS` and the per-component target properties
# exist only once ESP-IDF has registered the components.
#
# `out_var` receives the component library targets in the caller's scope. Plain
# executables are not component libraries; the caller passes those to the flag
# function by name.
function(thermo_own_component_targets out_var)
    idf_build_get_property(idf_path IDF_PATH)
    idf_build_get_property(build_components BUILD_COMPONENTS)

    set(targets)
    foreach(component IN LISTS build_components)
        idf_component_get_property(component_dir ${component} COMPONENT_DIR)
        idf_component_get_property(component_lib ${component} COMPONENT_LIB)

        get_target_property(component_type ${component_lib} TYPE)
        if(component_type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()

        string(FIND "${component_dir}/" "${idf_path}/" idf_pos)
        string(FIND "${component_dir}/" "/third_party/" third_party_pos)
        if(idf_pos EQUAL 0 OR third_party_pos GREATER -1)
            continue()
        endif()

        list(APPEND targets ${component_lib})
    endforeach()

    set(${out_var} "${targets}" PARENT_SCOPE)
endfunction()
