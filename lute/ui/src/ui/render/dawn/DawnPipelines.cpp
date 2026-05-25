#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN
#include <cstddef>

namespace lute::ui::dawn
{

bool DawnBackend::ensurePipelines(wgpu::TextureFormat format)
{
    if (pipelineFormat != format)
    {
        pipelineFormat = format;
        solidPipeline = {};
        textPipeline = {};
        paintTextPipeline = {};
        imageGlyphPipeline = {};
        surfaceBindGroup = {};
        textBindGroup = {};
    }

    return ensureSolidPipeline(format) && ensureTextPipeline(format) && ensureImageGlyphPipeline(format);
}

bool DawnBackend::ensureSolidPipeline(wgpu::TextureFormat format)
{
    if (solidPipeline && surfaceBindGroup)
        return true;

    if (!surfaceUniformBuffer)
        surfaceUniformBuffer = createBuffer(device, sizeof(SurfaceUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);

    wgpu::BindGroupLayoutEntry uniformLayout;
    uniformLayout.binding = 0;
    uniformLayout.visibility = wgpu::ShaderStage::Vertex;
    uniformLayout.buffer.type = wgpu::BufferBindingType::Uniform;
    uniformLayout.buffer.minBindingSize = sizeof(SurfaceUniforms);

    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
    bindGroupLayoutDescriptor.entryCount = 1;
    bindGroupLayoutDescriptor.entries = &uniformLayout;
    surfaceBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = &surfaceBindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    wgpu::BindGroupEntry uniformEntry;
    uniformEntry.binding = 0;
    uniformEntry.buffer = surfaceUniformBuffer;
    uniformEntry.size = sizeof(SurfaceUniforms);

    wgpu::BindGroupDescriptor bindGroupDescriptor;
    bindGroupDescriptor.layout = surfaceBindGroupLayout;
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = &uniformEntry;
    surfaceBindGroup = device.CreateBindGroup(&bindGroupDescriptor);

    std::string shaderSource = R"(
struct Uniforms {
  viewport: vec2f,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexIn {
  @location(0) position: vec2f,
  @location(1) local: vec2f,
  @location(2) rect: vec4f,
  @location(3) color: vec4f,
};

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) local: vec2f,
  @location(1) rect: vec4f,
  @location(2) color: vec4f,
};

fn clip_from_pixel(position: vec2f) -> vec4f {
  let x = position.x / u.viewport.x * 2.0 - 1.0;
  let y = 1.0 - position.y / u.viewport.y * 2.0;
  return vec4f(x, y, 0.0, 1.0);
}

@vertex fn vs(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  out.position = clip_from_pixel(in.position);
  out.local = in.local;
  out.rect = in.rect;
  out.color = in.color;
  return out;
}

@fragment fn fs(in: VertexOut) -> @location(0) vec4f {
  let radius = in.rect.z;
  if (radius <= 0.0) {
    return in.color;
  }

  let half_size = in.rect.xy;
  let p = in.local - half_size;
  let q = abs(p) - (half_size - vec2f(radius));
  let distance = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - radius;
  let coverage = 1.0 - smoothstep(-1.0, 1.0, distance);
  return vec4f(in.color.rgb, in.color.a * coverage);
}
)";

    wgpu::ShaderModule shader = createShaderModule(device, shaderSource);

    wgpu::VertexAttribute attributes[4];
    attributes[0].format = wgpu::VertexFormat::Float32x2;
    attributes[0].offset = offsetof(SolidVertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(SolidVertex, local);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Float32x4;
    attributes[2].offset = offsetof(SolidVertex, rect);
    attributes[2].shaderLocation = 2;
    attributes[3].format = wgpu::VertexFormat::Float32x4;
    attributes[3].offset = offsetof(SolidVertex, color);
    attributes[3].shaderLocation = 3;

    wgpu::VertexBufferLayout vertexBufferLayout;
    vertexBufferLayout.arrayStride = sizeof(SolidVertex);
    vertexBufferLayout.attributeCount = 4;
    vertexBufferLayout.attributes = attributes;

    wgpu::BlendState blend;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;

    wgpu::ColorTargetState colorTarget;
    colorTarget.format = format;
    colorTarget.blend = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment;
    fragment.module = shader;
    fragment.entryPoint = "fs";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDescriptor;
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.vertex.module = shader;
    pipelineDescriptor.vertex.entryPoint = "vs";
    pipelineDescriptor.vertex.bufferCount = 1;
    pipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
    pipelineDescriptor.fragment = &fragment;

    solidPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
    return solidPipeline && surfaceBindGroup;
}

bool DawnBackend::ensureTextPipeline(wgpu::TextureFormat format)
{
#if !LUTE_UI_USE_HARFBUZZ_GPU
    (void)format;
    return true;
#else
    if (textPipeline && paintTextPipeline && textBindGroup)
        return true;

    if (!textUniformBuffer)
        textUniformBuffer = createBuffer(device, sizeof(TextUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);

    if (!atlasBuffer)
    {
        if (atlasShadow.empty())
            atlasShadow.assign(kAtlasCapacity * 4, 0);
        else if (atlasShadow.size() != kAtlasCapacity * 4)
            atlasShadow.resize(kAtlasCapacity * 4);
        atlasBuffer = createBuffer(device, kAtlasCapacity * 4 * sizeof(int32_t), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
        markAtlasDirtyRange(0, atlasCursor);
    }

    wgpu::BindGroupLayoutEntry entries[2];
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.minBindingSize = sizeof(TextUniforms);
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].buffer.minBindingSize = kAtlasCapacity * 4 * sizeof(int32_t);

    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
    bindGroupLayoutDescriptor.entryCount = 2;
    bindGroupLayoutDescriptor.entries = entries;
    textBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = &textBindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    wgpu::BindGroupEntry bindGroupEntries[2];
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = textUniformBuffer;
    bindGroupEntries[0].size = sizeof(TextUniforms);
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].buffer = atlasBuffer;
    bindGroupEntries[1].size = kAtlasCapacity * 4 * sizeof(int32_t);

    wgpu::BindGroupDescriptor bindGroupDescriptor;
    bindGroupDescriptor.layout = textBindGroupLayout;
    bindGroupDescriptor.entryCount = 2;
    bindGroupDescriptor.entries = bindGroupEntries;
    textBindGroup = device.CreateBindGroup(&bindGroupDescriptor);

    std::string commonShaderSource;
    commonShaderSource += hb_gpu_shader_source(HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_LANG_WGSL);
    commonShaderSource += "\n";
    commonShaderSource += hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_LANG_WGSL);
    commonShaderSource += "\n";
    commonShaderSource += hb_gpu_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
    commonShaderSource += "\n";
    commonShaderSource += hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
    commonShaderSource += "\n";
    commonShaderSource += R"(
struct Uniforms {
  mvp: mat4x4f,
  viewport: vec2f,
  stem_darkening: f32,
  debug: f32,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<storage, read> hb_gpu_atlas: array<vec4<i32>>;

struct VertexInput {
  @location(0) position: vec2f,
  @location(1) texcoord: vec2f,
  @location(2) normal: vec2f,
  @location(3) emPerPos: f32,
  @location(4) glyphLoc: u32,
  @location(5) color: vec4f,
  @location(6) background: vec4f,
};

struct VertexOutput {
  @builtin(position) clip_position: vec4f,
  @location(0) texcoord: vec2f,
  @location(1) @interpolate(flat) glyphLoc: u32,
  @location(2) color: vec4f,
  @location(3) background: vec4f,
};

fn srgb_to_linear_channel(value: f32) -> f32 {
  let c = clamp(value, 0.0, 1.0);
  return select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045);
}

fn linear_to_srgb_channel(value: f32) -> f32 {
  let c = clamp(value, 0.0, 1.0);
  return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308);
}

fn srgb_to_linear(color: vec3f) -> vec3f {
  return vec3f(
    srgb_to_linear_channel(color.r),
    srgb_to_linear_channel(color.g),
    srgb_to_linear_channel(color.b)
  );
}

fn linear_to_srgb(color: vec3f) -> vec3f {
  return vec3f(
    linear_to_srgb_channel(color.r),
    linear_to_srgb_channel(color.g),
    linear_to_srgb_channel(color.b)
  );
}

fn min_premul_alpha_for_color(desired: vec3f, background: vec3f) -> f32 {
  let lower = (background - desired) / max(background, vec3f(0.00001));
  let upper = (desired - background) / max(vec3f(1.0) - background, vec3f(0.00001));
  let required = vec3f(
    select(lower.r, upper.r, desired.r >= background.r),
    select(lower.g, upper.g, desired.g >= background.g),
    select(lower.b, upper.b, desired.b >= background.b)
  );
  return clamp(max(max(required.r, required.g), required.b), 0.0, 1.0);
}

fn srgb_coverage_premul(coverage: f32, color: vec4f, background: vec3f) -> vec4f {
  let fg = clamp(color.rgb, vec3f(0.0), vec3f(1.0));
  let bg = clamp(background, vec3f(0.0), vec3f(1.0));
  let desired = srgb_to_linear(mix(linear_to_srgb(bg), linear_to_srgb(fg), vec3f(clamp(coverage * color.a, 0.0, 1.0))));
  let alpha = min_premul_alpha_for_color(desired, bg);
  let premul = desired - bg * (1.0 - alpha);
  return vec4f(clamp(premul, vec3f(0.0), vec3f(1.0)), alpha);
}

fn coverage_to_premul(color: vec4f, background: vec4f, coverage: f32) -> vec4f {
  if (background.a >= 0.0 && color.a > 0.0) {
    return srgb_coverage_premul(coverage, color, background.rgb);
  }
  return vec4f(color.rgb * color.a * coverage, color.a * coverage);
}

@vertex fn vs_main(in: VertexInput) -> VertexOutput {
  var pos = in.position;
  var tc = in.texcoord;
  let jac = vec4f(in.emPerPos, 0.0, 0.0, -in.emPerPos);
  let result = hb_gpu_dilate(pos, tc, in.normal, jac, u.mvp, u.viewport);
  pos = result[0];
  tc = result[1];

  var out: VertexOutput;
  out.clip_position = u.mvp * vec4f(pos, 0.0, 1.0);
  out.texcoord = tc;
  out.glyphLoc = in.glyphLoc;
  out.color = in.color;
  out.background = in.background;
  return out;
}
)";

    std::string drawShaderSource = commonShaderSource + R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  let ppem = hb_gpu_ppem(in.texcoord, in.glyphLoc, &hb_gpu_atlas);
  let cov = hb_gpu_draw(in.texcoord, in.glyphLoc, &hb_gpu_atlas);
  var adjusted = cov;

  if (cov > 0.0 && cov < 1.0) {
    if (u.stem_darkening > 0.0) {
      let brightness = dot(in.color.rgb, vec3f(1.0 / 3.0));
      adjusted = hb_gpu_stem_darken(adjusted, brightness, ppem);
    }
  }

  return coverage_to_premul(in.color, in.background, adjusted);
}
)";

    std::string paintShaderSource = commonShaderSource;
    paintShaderSource += hb_gpu_paint_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
    paintShaderSource += "\n";
    paintShaderSource += R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  let ppem = hb_gpu_ppem(in.texcoord, in.glyphLoc, &hb_gpu_atlas);
  var cov: f32;
  var c = hb_gpu_paint(in.texcoord, in.glyphLoc, in.color, &hb_gpu_atlas, &cov);

  if (cov > 0.0 && cov < 1.0) {
    var adjusted = cov;
    if (u.stem_darkening > 0.0) {
      let brightness = select(0.0, dot(c.rgb, vec3f(1.0 / 3.0)) / c.a, c.a > 0.0);
      adjusted = hb_gpu_stem_darken(adjusted, brightness, ppem);
    }
    if (in.background.a >= 0.0 && c.a > 0.0) {
      let straight = c.rgb / c.a;
      let opacity = clamp(c.a / cov, 0.0, 1.0);
      c = coverage_to_premul(vec4f(straight, opacity), in.background, adjusted);
    } else {
      c = c * (adjusted / cov);
    }
  }

  return c;
}
)";

    wgpu::ShaderModule drawShader = createShaderModule(device, drawShaderSource);
    wgpu::ShaderModule paintShader = createShaderModule(device, paintShaderSource);

    wgpu::VertexAttribute attributes[7];
    attributes[0].format = wgpu::VertexFormat::Float32x2;
    attributes[0].offset = offsetof(GlyphVertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(GlyphVertex, texcoord);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Float32x2;
    attributes[2].offset = offsetof(GlyphVertex, normal);
    attributes[2].shaderLocation = 2;
    attributes[3].format = wgpu::VertexFormat::Float32;
    attributes[3].offset = offsetof(GlyphVertex, emPerPos);
    attributes[3].shaderLocation = 3;
    attributes[4].format = wgpu::VertexFormat::Uint32;
    attributes[4].offset = offsetof(GlyphVertex, atlasOffset);
    attributes[4].shaderLocation = 4;
    attributes[5].format = wgpu::VertexFormat::Float32x4;
    attributes[5].offset = offsetof(GlyphVertex, color);
    attributes[5].shaderLocation = 5;
    attributes[6].format = wgpu::VertexFormat::Float32x4;
    attributes[6].offset = offsetof(GlyphVertex, background);
    attributes[6].shaderLocation = 6;

    wgpu::VertexBufferLayout vertexBufferLayout;
    vertexBufferLayout.arrayStride = sizeof(GlyphVertex);
    vertexBufferLayout.attributeCount = 7;
    vertexBufferLayout.attributes = attributes;

    wgpu::BlendState blend;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;

    wgpu::ColorTargetState colorTarget;
    colorTarget.format = format;
    colorTarget.blend = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDescriptor;
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.vertex.entryPoint = "vs_main";
    pipelineDescriptor.vertex.bufferCount = 1;
    pipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
    pipelineDescriptor.fragment = &fragment;

    pipelineDescriptor.vertex.module = drawShader;
    fragment.module = drawShader;
    textPipeline = device.CreateRenderPipeline(&pipelineDescriptor);

    pipelineDescriptor.vertex.module = paintShader;
    fragment.module = paintShader;
    paintTextPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
    return textPipeline && paintTextPipeline && textBindGroup;
#endif
}

bool DawnBackend::ensureImageGlyphPipeline(wgpu::TextureFormat format)
{
#if !LUTE_UI_USE_HARFBUZZ_GPU
    (void)format;
    return true;
#else
    if (!ensureImageGlyphAtlasResources())
        return false;

    if (imageGlyphPipeline && imageGlyphBindGroup)
        return true;

    wgpu::BindGroupLayout layouts[2] = {surfaceBindGroupLayout, imageGlyphBindGroupLayout};
    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
    pipelineLayoutDescriptor.bindGroupLayoutCount = 2;
    pipelineLayoutDescriptor.bindGroupLayouts = layouts;
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    std::string shaderSource = R"(
struct Uniforms {
  viewport: vec2f,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var image_atlas: texture_2d<f32>;
@group(1) @binding(1) var image_sampler: sampler;

struct VertexInput {
  @location(0) position: vec2f,
  @location(1) texcoord: vec2f,
};

struct VertexOutput {
  @builtin(position) clip_position: vec4f,
  @location(0) texcoord: vec2f,
};

fn clip_from_pixel(position: vec2f) -> vec4f {
  let x = position.x / u.viewport.x * 2.0 - 1.0;
  let y = 1.0 - position.y / u.viewport.y * 2.0;
  return vec4f(x, y, 0.0, 1.0);
}

@vertex fn vs_main(in: VertexInput) -> VertexOutput {
  var out: VertexOutput;
  out.clip_position = clip_from_pixel(in.position);
  out.texcoord = in.texcoord;
  return out;
}

@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  return textureSample(image_atlas, image_sampler, in.texcoord);
}
)";

    wgpu::ShaderModule shader = createShaderModule(device, shaderSource);

    wgpu::VertexAttribute attributes[2];
    attributes[0].format = wgpu::VertexFormat::Float32x2;
    attributes[0].offset = offsetof(ImageGlyphVertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(ImageGlyphVertex, texcoord);
    attributes[1].shaderLocation = 1;

    wgpu::VertexBufferLayout vertexBufferLayout;
    vertexBufferLayout.arrayStride = sizeof(ImageGlyphVertex);
    vertexBufferLayout.attributeCount = 2;
    vertexBufferLayout.attributes = attributes;

    wgpu::BlendState blend;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;

    wgpu::ColorTargetState colorTarget;
    colorTarget.format = format;
    colorTarget.blend = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment;
    fragment.module = shader;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDescriptor;
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.vertex.module = shader;
    pipelineDescriptor.vertex.entryPoint = "vs_main";
    pipelineDescriptor.vertex.bufferCount = 1;
    pipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
    pipelineDescriptor.fragment = &fragment;

    imageGlyphPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
    return imageGlyphPipeline && imageGlyphBindGroup;
#endif
}

} // namespace lute::ui::dawn
#endif
