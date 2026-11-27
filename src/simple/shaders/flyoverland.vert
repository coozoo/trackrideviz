#version 440
// The flyover's land on the graphics card: each point seen through the flyover's camera, put
// into the gauge's rectangle; w kept, so that the map and the haze follow the perspective.

layout(location = 0) in vec3 position;   // east, north, height: meters around the land's middle
layout(location = 1) in vec2 texCoord;   // the map's u and v
layout(location = 2) in vec4 colorLight; // the land's own color (no map), the sun's light on it

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColorLight;
layout(location = 2) out float vDistance;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    mat4 toClip;
    vec4 rect;      // x, y, width, height: the gauge's place in the item
    vec4 haze;      // its color; w: 1 with a map, 0 without
    vec4 params;    // the hills' exaggeration, where the haze begins and is whole, the opacity
};

void main()
{
    vec4 clip = toClip * vec4(position.xy, position.z * params.x, 1.0);
    vTexCoord = texCoord;
    vColorLight = colorLight;
    vDistance = clip.w;
    // from clip space to the rectangle, still divided by w later
    vec4 placed = vec4(rect.x * clip.w + (clip.x + clip.w) * 0.5 * rect.z,
                       rect.y * clip.w + (clip.w - clip.y) * 0.5 * rect.w,
                       0.0, clip.w);
    gl_Position = qt_Matrix * placed;
}
