#version 330

in vec3 vViewSpacePosition;
in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

uniform vec3 uLightDirection;
uniform vec3 uLightIntensity;

out vec3 fColor;

void main()
{
   vec3 viewSpaceNormal=normalize(vViewSpaceNormal);
   fColor=vec3(1. / 3.1415) * uLightIntensity * dot(viewSpaceNormal, uLightDirection);
}