# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Bake 3D meshes (.obj -> .binobj) at build time, so the repository stores the
# human-readable source meshes instead of binary blobs. Every *.obj found under
# Resources/art is baked and exposed under the same :/gui/<relative>.binobj path,
# so new models just need to be dropped in next to the existing ones.
#
# Usage: include(cmake/generate_models.cmake)
# Requires: target "Telegram" and function "nice_target_sources" to exist.

set(_models_art_dir "${CMAKE_CURRENT_SOURCE_DIR}/Resources/art")
file(GLOB_RECURSE _model_sources "${_models_art_dir}/*.obj")

if (_model_sources STREQUAL "")
    return()
endif()

find_package(Python3 REQUIRED)

set(_models_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/obj2binobj.py")
set(_models_out_dir "${CMAKE_CURRENT_BINARY_DIR}/models")
set(_model_outputs)
set(_qrc_files)
foreach(_src ${_model_sources})
    file(RELATIVE_PATH _rel "${_models_art_dir}" "${_src}")
    string(REGEX REPLACE "\\.obj$" ".binobj" _rel_bin "${_rel}")
    set(_out "${_models_out_dir}/${_rel_bin}")
    get_filename_component(_out_subdir "${_out}" DIRECTORY)
    file(MAKE_DIRECTORY ${_out_subdir})

    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${Python3_EXECUTABLE} ${_models_script} ${_src} ${_out}
        DEPENDS ${_models_script} ${_src}
        COMMENT "Baking model: ${_rel} -> ${_rel_bin}"
        VERBATIM)
    list(APPEND _model_outputs ${_out})

    string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" _safe_name "${_rel_bin}")
    set(_qrc_path "${_models_out_dir}/models_${_safe_name}.qrc")
    set(_qrc_new "<RCC>\n    <qresource prefix=\"/gui\">\n        <file alias=\"art/${_rel_bin}\">${_rel_bin}</file>\n    </qresource>\n</RCC>\n")
    set(_qrc_old "")
    if (EXISTS "${_qrc_path}")
        file(READ "${_qrc_path}" _qrc_old)
    endif()
    if (NOT "${_qrc_new}" STREQUAL "${_qrc_old}")
        file(WRITE "${_qrc_path}" "${_qrc_new}")
    endif()

    set_source_files_properties("${_qrc_path}" PROPERTIES
        QRC_GENERATED_FROM "${_models_script};${_src}")
    list(APPEND _qrc_files "${_qrc_path}")
endforeach()

add_custom_target(bake_models DEPENDS ${_model_outputs})

add_library(telegram_models OBJECT)
target_sources(telegram_models PRIVATE ${_qrc_files})
set_target_properties(telegram_models PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC ON
)
if (MSVC)
    target_compile_options(telegram_models PRIVATE /bigobj)
endif()
target_link_libraries(telegram_models PRIVATE desktop-app::external_qt)
add_dependencies(telegram_models bake_models)

target_link_libraries(Telegram PRIVATE telegram_models)
message(STATUS "Models: will bake ${_models_art_dir}/**/*.obj -> .binobj")
