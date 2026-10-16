#version 440
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(std140,binding=0) uniform Display {vec4 row0;vec4 row1;vec4 row2;vec4 config;} display;
layout(binding=1) uniform sampler2D workImage;
layout(binding=2) uniform sampler2D encoding;
layout(binding=3) uniform sampler2D deviceCodes;
float encode(float v){
    int i=int(clamp(v,0.0,1.0)*65535.0+0.5);
    return texelFetch(encoding,ivec2(i%256,i/256),0).r;
}
void main(){
    vec4 work=texture(workImage,uv);
    vec3 linear=vec3(dot(display.row0.xyz,work.rgb),dot(display.row1.xyz,work.rgb),dot(display.row2.xyz,work.rgb));
    vec3 encoded=vec3(encode(linear.r),encode(linear.g),encode(linear.b));
    if(display.config.x>0.0){
        ivec3 code=ivec3(encoded*255.0+0.5);
        encoded=texelFetch(deviceCodes,ivec2(code.r+(code.g%16)*256,code.b+(code.g/16)*256),0).rgb;
    }
    color=vec4(encoded*work.a,work.a);
}
