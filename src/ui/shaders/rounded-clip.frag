#version 440

// From this directory, use Qt 6.4's qsb:
// qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o rounded-clip.frag.qsb rounded-clip.frag
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 qt_SubRect_source;
    vec2 artSize;
    vec2 imageScale;
    float cornerRadius;
    vec4 background;
    vec4 accent;
    float progress;
    float highlight;
    float imageReady;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 halfSize = artSize * 0.5;
    vec2 corner = abs(qt_TexCoord0 * artSize - halfSize) - halfSize + cornerRadius;
    float distanceToEdge = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    float coverage = clamp(0.5 - distanceToEdge / max(fwidth(distanceToEdge), 0.001), 0.0, 1.0);

    vec4 color = background;
    vec2 uv = (qt_TexCoord0 - 0.5) * imageScale + 0.5;
    if (imageReady > 0.5 && all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)))) {
        vec4 pixel = texture(source, qt_SubRect_source.xy + uv * qt_SubRect_source.zw);
        color = pixel + color * (1.0 - pixel.a);
    }
    color = mix(color, vec4(1.0), highlight);
    if (progress > 0.0 && qt_TexCoord0.y * artSize.y >= artSize.y - 4.0) {
        color = qt_TexCoord0.x <= progress ? accent : color * (1.0 - 160.0 / 255.0) + vec4(0.0, 0.0, 0.0, 160.0 / 255.0);
    }
    fragColor = color * (qt_Opacity * coverage);
}
