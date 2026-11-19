#version 440

// From this directory, use Qt 6.4's qsb:
// qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o rounded-clip.frag.qsb rounded-clip.frag
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 artSize;
    float cornerRadius;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 halfSize = artSize * 0.5;
    vec2 corner = abs(qt_TexCoord0 * artSize - halfSize) - halfSize + cornerRadius;
    float distanceToEdge = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    // The texture's rectangular edges already clip the straight segments.
    float coverage = clamp(0.5 - distanceToEdge / max(fwidth(distanceToEdge), 0.001), 0.0, 1.0);
    fragColor = texture(source, qt_TexCoord0) * (qt_Opacity * coverage);
}
