#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderPrecision { int rangeMin, rangeMax, precision; };

struct WebGLProfile {
    std::string vendor = "Google Inc. (Intel)";
    std::string renderer = "ANGLE (Intel, Intel(R) UHD Graphics 620 Direct3D11 vs_5_0 ps_5_0)";
    std::string version = "WebGL 2.0 (OpenGL ES 3.0 Chromium)";
    std::string shading_language_version = "WebGL GLSL ES 3.00 (OpenGL ES GLSL ES 3.0 Chromium)";

    uint32_t max_texture_size = 16384;
    uint32_t max_cube_map_texture_size = 16384;
    uint32_t max_renderbuffer_size = 16384;
    uint32_t max_vertex_texture_image_units = 16;
    uint32_t max_texture_image_units = 16;
    uint32_t max_combined_texture_image_units = 32;
    uint32_t max_vertex_attribs = 16;
    uint32_t max_vertex_uniform_vectors = 4096;
    uint32_t max_fragment_uniform_vectors = 1024;
    uint32_t max_varying_vectors = 30;
    uint32_t max_viewport_dims = 16384;
    std::array<float, 2> aliased_point_size_range{1.0f, 1024.0f};
    std::array<float, 2> aliased_line_width_range{1.0f, 1.0f};

    uint32_t max_3d_texture_size = 2048;
    uint32_t max_array_texture_layers = 2048;
    uint32_t max_uniform_buffer_bindings = 72;
    uint32_t uniform_buffer_offset_alignment = 256;
    uint32_t max_samples = 8;
    uint64_t max_element_index = 4294967295ull;
    uint32_t max_elements_indices = 16777216;
    uint32_t max_elements_vertices = 16777216;
    float max_texture_lod_bias = 2.0f;
    uint32_t max_vertex_output_components = 128;
    uint32_t max_fragment_input_components = 124;
    uint32_t max_vertex_uniform_components = 16384;
    uint32_t max_fragment_uniform_components = 4096;
    uint32_t max_vertex_uniform_blocks = 14;
    uint32_t max_fragment_uniform_blocks = 14;
    uint32_t max_combined_uniform_blocks = 70;
    uint64_t max_combined_fragment_uniform_components = 4194304;
    uint64_t max_combined_vertex_uniform_components = 4194304;
    uint64_t max_uniform_block_size = 65536;
    uint64_t max_server_wait_timeout = 0;
    uint32_t max_varying_components = 120;

    ShaderPrecision vertex_high_float{127, 127, 23};
    ShaderPrecision vertex_medium_float{127, 127, 23};
    ShaderPrecision vertex_low_float{127, 127, 23};
    ShaderPrecision fragment_high_float{127, 127, 23};
    ShaderPrecision fragment_medium_float{127, 127, 23};
    ShaderPrecision fragment_low_float{127, 127, 23};
    ShaderPrecision vertex_high_int{31, 30, 0};
    ShaderPrecision vertex_medium_int{31, 30, 0};
    ShaderPrecision vertex_low_int{31, 30, 0};
    ShaderPrecision fragment_high_int{31, 30, 0};
    ShaderPrecision fragment_medium_int{31, 30, 0};
    ShaderPrecision fragment_low_int{31, 30, 0};

    std::vector<std::string> supported_extensions = {
        "ANGLE_instanced_arrays", "EXT_blend_minmax", "EXT_color_buffer_half_float",
        "EXT_float_blend", "EXT_frag_depth", "EXT_shader_texture_lod", "EXT_sRGB",
        "EXT_texture_compression_bptc", "EXT_texture_filter_anisotropic",
        "EXT_texture_norm16", "OES_element_index_uint", "OES_fbo_render_mipmap",
        "OES_standard_derivatives", "OES_texture_float", "OES_texture_float_linear",
        "OES_texture_half_float", "OES_texture_half_float_linear",
        "OES_vertex_array_object", "WEBGL_color_buffer_float",
        "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb",
        "WEBGL_debug_renderer_info", "WEBGL_debug_shaders", "WEBGL_depth_texture",
        "WEBGL_draw_buffers", "WEBGL_lose_context", "WEBGL_multi_draw",
    };

    bool antialias = true;
    bool fail_if_major_performance_caveat = false;
};

// Perturbs raw readPixels output the same way canvas is perturbed — same per-site key.
void PerturbReadPixels(uint8_t* rgba, size_t width, size_t height,
                        class SessionKeyStore& keys, const std::string& origin);