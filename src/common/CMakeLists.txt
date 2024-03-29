# Add a custom command to generate a new shader_cache_version hash when any of the following files change
# NOTE: This is an approximation of what files affect shader generation, its possible something else
# could affect the result, but much more unlikely than the following files. Keeping a list of files
# like this allows for much better caching since it doesn't force the user to recompile binary shaders every update
set(VIDEO_CORE "${CMAKE_SOURCE_DIR}/src/video_core")
if (DEFINED ENV{CI})
  if (DEFINED ENV{TRAVIS})
    set(BUILD_REPOSITORY $ENV{TRAVIS_REPO_SLUG})
    set(BUILD_TAG $ENV{TRAVIS_TAG})
  elseif(DEFINED ENV{APPVEYOR})
    set(BUILD_REPOSITORY $ENV{APPVEYOR_REPO_NAME})
    set(BUILD_TAG $ENV{APPVEYOR_REPO_TAG_NAME})
  elseif(DEFINED ENV{AZURE})
    set(BUILD_REPOSITORY $ENV{AZURE_REPO_NAME})
    set(BUILD_TAG $ENV{AZURE_REPO_TAG})
  endif()
endif()
if (DEFINED ENV{TITLEBARFORMATIDLE})
  set(TITLE_BAR_FORMAT_IDLE $ENV{TITLEBARFORMATIDLE})
endif ()
if (DEFINED ENV{TITLEBARFORMATRUNNING})
  set(TITLE_BAR_FORMAT_RUNNING $ENV{TITLEBARFORMATRUNNING})
endif ()
if (DEFINED ENV{DISPLAYVERSION})
  set(DISPLAY_VERSION $ENV{DISPLAYVERSION})
endif ()
add_custom_command(OUTPUT scm_rev.cpp
    COMMAND ${CMAKE_COMMAND}
      -DSRC_DIR="${CMAKE_SOURCE_DIR}"
      -DBUILD_REPOSITORY="${BUILD_REPOSITORY}"
      -DTITLE_BAR_FORMAT_IDLE="${TITLE_BAR_FORMAT_IDLE}"
      -DTITLE_BAR_FORMAT_RUNNING="${TITLE_BAR_FORMAT_RUNNING}"
      -DBUILD_TAG="${BUILD_TAG}"
      -DBUILD_ID="${DISPLAY_VERSION}"
      -P "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateSCMRev.cmake"
    DEPENDS
      # WARNING! It was too much work to try and make a common location for this list,
      # so if you need to change it, please update CMakeModules/GenerateSCMRev.cmake as well
      "${VIDEO_CORE}/renderer_opengl/gl_shader_cache.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_cache.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_decompiler.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_decompiler.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_gen.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_gen.h"
      "${VIDEO_CORE}/shader/decode/arithmetic.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_half.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_half_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_integer.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_integer_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/bfe.cpp"
      "${VIDEO_CORE}/shader/decode/bfi.cpp"
      "${VIDEO_CORE}/shader/decode/conversion.cpp"
      "${VIDEO_CORE}/shader/decode/ffma.cpp"
      "${VIDEO_CORE}/shader/decode/float_set.cpp"
      "${VIDEO_CORE}/shader/decode/float_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/half_set.cpp"
      "${VIDEO_CORE}/shader/decode/half_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/hfma2.cpp"
      "${VIDEO_CORE}/shader/decode/image.cpp"
      "${VIDEO_CORE}/shader/decode/integer_set.cpp"
      "${VIDEO_CORE}/shader/decode/integer_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/memory.cpp"
      "${VIDEO_CORE}/shader/decode/texture.cpp"
      "${VIDEO_CORE}/shader/decode/other.cpp"
      "${VIDEO_CORE}/shader/decode/predicate_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/predicate_set_register.cpp"
      "${VIDEO_CORE}/shader/decode/register_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/shift.cpp"
      "${VIDEO_CORE}/shader/decode/video.cpp"
      "${VIDEO_CORE}/shader/decode/warp.cpp"
      "${VIDEO_CORE}/shader/decode/xmad.cpp"
      "${VIDEO_CORE}/shader/ast.cpp"
      "${VIDEO_CORE}/shader/ast.h"
      "${VIDEO_CORE}/shader/control_flow.cpp"
      "${VIDEO_CORE}/shader/control_flow.h"
      "${VIDEO_CORE}/shader/compiler_settings.cpp"
      "${VIDEO_CORE}/shader/compiler_settings.h"
      "${VIDEO_CORE}/shader/decode.cpp"
      "${VIDEO_CORE}/shader/expr.cpp"
      "${VIDEO_CORE}/shader/expr.h"
      "${VIDEO_CORE}/shader/node.h"
      "${VIDEO_CORE}/shader/node_helper.cpp"
      "${VIDEO_CORE}/shader/node_helper.h"
      "${VIDEO_CORE}/shader/shader_ir.cpp"
      "${VIDEO_CORE}/shader/shader_ir.h"
      "${VIDEO_CORE}/shader/track.cpp"
      # and also check that the scm_rev files haven't changed
      "${CMAKE_CURRENT_SOURCE_DIR}/scm_rev.cpp.in"
      "${CMAKE_CURRENT_SOURCE_DIR}/scm_rev.h"
      # technically we should regenerate if the git version changed, but its not worth the effort imo
      "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateSCMRev.cmake"
)

add_library(common STATIC
    algorithm.h
    alignment.h
    assert.h
    detached_tasks.cpp
    detached_tasks.h
    bit_field.h
    bit_util.h
    cityhash.cpp
    cityhash.h
    color.h
    common_funcs.h
    common_paths.h
    common_types.h
    file_util.cpp
    file_util.h
    hash.h
    hex_util.cpp
    hex_util.h
    logging/backend.cpp
    logging/backend.h
    logging/filter.cpp
    logging/filter.h
    logging/log.h
    logging/text_formatter.cpp
    logging/text_formatter.h
    lz4_compression.cpp
    lz4_compression.h
    math_util.h
    memory_hook.cpp
    memory_hook.h
    microprofile.cpp
    microprofile.h
    microprofileui.h
    misc.cpp
    multi_level_queue.h
    page_table.cpp
    page_table.h
    param_package.cpp
    param_package.h
    quaternion.h
    ring_buffer.h
    scm_rev.cpp
    scm_rev.h
    scope_exit.h
    string_util.cpp
    string_util.h
    swap.h
    telemetry.cpp
    telemetry.h
    thread.cpp
    thread.h
    thread_queue_list.h
    threadsafe_queue.h
    timer.cpp
    timer.h
    uint128.cpp
    uint128.h
    uuid.cpp
    uuid.h
    vector_math.h
    web_result.h
    zstd_compression.cpp
    zstd_compression.h
)

if(ARCHITECTURE_x86_64)
    target_sources(common
        PRIVATE
            x64/cpu_detect.cpp
            x64/cpu_detect.h
    )
endif()

create_target_directory_groups(common)

target_link_libraries(common PUBLIC Boost::boost fmt microprofile)
target_link_libraries(common PRIVATE lz4_static libzstd_static)
