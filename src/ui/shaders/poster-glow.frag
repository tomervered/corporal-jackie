#version 440

// Bake with the same Qt 6.4 qsb targets as rounded-clip.frag.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 artSize;
    float cornerRadius;
    float spread;
    vec4 accent;
};

void main()
{
    vec2 halfSize = artSize * 0.5;
    vec2 point = (qt_TexCoord0 - 0.5) * (artSize + 2.0 * spread);
    vec2 corner = abs(point) - halfSize + cornerRadius;
    float distanceToEdge = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    float outside = smoothstep(-0.5, 0.5, distanceToEdge);
    float falloff = max(0.0, 1.0 - distanceToEdge / spread);
    fragColor = accent * (qt_Opacity * outside * 0.45 * falloff * falloff);
}
