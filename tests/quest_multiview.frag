#version 450
#extension GL_EXT_multiview : require
layout(set=0,binding=0) uniform sampler2DArray layered;
layout(set=0,binding=1) uniform sampler2DArray sharedTexture;
layout(push_constant) uniform Params { vec2 depth; int mode; } p;
layout(location=0) out vec4 color;
#ifdef VERTEX_LAYOUT_INPUT
layout(location=0) in vec4 inputColor;
#endif
void main() {
    vec4 eye = texture(layered,vec3(gl_FragCoord.xy/16.0,gl_ViewIndex));
    vec4 commonColor = texture(sharedTexture,vec3(0.5,0.5,0));
    color = p.mode==1 ? vec4(0,0,1,1) : p.mode>=2 ? eye : vec4(eye.rg,commonColor.b,1);
#ifdef VERTEX_LAYOUT_INPUT
    color*=inputColor;
#endif
}
