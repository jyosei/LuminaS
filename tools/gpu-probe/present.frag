#version 440
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(std140,binding=0) uniform Display {vec4 row0;vec4 row1;vec4 row2;} display;
layout(binding=1) uniform sampler2D workImage;
layout(binding=2) uniform sampler2D encoding;
float encode(float v){
    int i=int(clamp(v,0.0,1.0)*65535.0+0.5);
    return texelFetch(encoding,ivec2(i%256,i/256),0).r;
}
void main(){
    vec4 work=texture(workImage,uv);
    vec3 linear=vec3(dot(display.row0.xyz,work.rgb),dot(display.row1.xyz,work.rgb),dot(display.row2.xyz,work.rgb));
    color=vec4(vec3(encode(linear.r),encode(linear.g),encode(linear.b))*work.a,work.a);
}
