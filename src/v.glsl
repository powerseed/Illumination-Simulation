#version 150

in vec3 vColour;
in float xPos;
uniform float Y;
uniform vec2 Window;
uniform mat4 Projection;
out vec3 colour;

void main()
{
   colour = vColour;
   //gl_Position = Projection * vec4( (xPos+0.5) * 2 / Window.x - 1, (Y+0.5) * 2 / Window.y - 1, 0, 1);
   gl_Position = vec4( (xPos+0.5) * 2 / Window.x - 1, (Y+0.5) * 2 / Window.y - 1, 0, 1);
   // gl_Position = vec4(xPos * 2 / Window.x - 1, 1-Y * 2 / Window.y, 0, 1);
}
