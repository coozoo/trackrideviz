#version 440
// The land's color: the map (or its own color) in the sun's light, fading into the haze far
// away, as the software picture draws it.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColorLight;
layout(location = 2) in float vDistance;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    mat4 toClip;
    vec4 rect;
    vec4 haze;
    vec4 params;
};

layout(binding = 1) uniform sampler2D map;

void main()
{
    vec3 ground = haze.w > 0.5 ? texture(map, vTexCoord).rgb : vColorLight.rgb;
    float share = clamp((vDistance - params.y) / (params.z - params.y), 0.0, 1.0);
    float hazed = share > 0.0 ? pow(share, 1.6) : 0.0;
    vec3 color = ground * vColorLight.a * (1.0 - hazed) + haze.rgb * hazed;
    fragColor = vec4(min(color, vec3(1.0)), 1.0) * params.w;
}
