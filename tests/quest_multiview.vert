#version 450
#extension GL_EXT_multiview : require
layout(push_constant) uniform Params { vec2 depth; int mode; } p;
#ifdef VERTEX_LAYOUT_INPUT
layout(location=0) in vec2 vertexPosition;
layout(location=1) in vec4 vertexColor;
layout(location=0) out vec4 inputColor;
#endif
void main() {
    const vec2 positions[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position = vec4(positions[gl_VertexIndex],p.depth[gl_ViewIndex],1);
#ifdef VERTEX_LAYOUT_INPUT
    gl_Position.xy=vertexPosition;
    inputColor=vertexColor;
#endif
    if(p.mode==1) gl_Position.x=gl_Position.x*0.5+0.5;
    if(p.mode==3) gl_Position.xy*=0.875; // leave edge pixels untouched
}
