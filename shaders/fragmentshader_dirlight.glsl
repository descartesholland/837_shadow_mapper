#version 330
// fragment shader for phong-model lighting with a single directional light source.

in vec4 var_Color;
in vec3 var_Normal;
in vec3 var_Position;

uniform vec3 camPos;

uniform vec3 diffColor;
uniform vec3 specColor;
uniform vec3 ambientColor;
uniform float shininess;
uniform float alpha;
uniform sampler2D diffuseTex;
uniform sampler2D shadowTex;
uniform mat4 light_VP;

uniform vec3 lightPos;
uniform vec3 lightDiff;

layout(location=0) out vec4 out_Color;

vec4 blinn_phong(vec3 kd) {
    // Implement Blinn-Phong Shading Model
    // 1. Convert everything to world space
    //    and normalize directions
    vec4 pos_world = vec4(var_Position, 1);
    vec3 normal_world = normalize(var_Normal);
    pos_world /= pos_world.w;
    vec3 light_dir = normalize(lightPos);
    vec3 cam_dir = camPos - pos_world.xyz;
    cam_dir = normalize(cam_dir);

    // 2. Compute Diffuse Contribution
    float ndotl = max(dot(normal_world, light_dir), 0.0);
    vec3 diffContrib = lightDiff * kd * ndotl;

    // 3. Compute Specular Contribution
    vec3 R = reflect( -light_dir, normal_world );
    float eyedotr = max(dot(cam_dir, R), 0.0);
    vec3 specContrib = pow(eyedotr, shininess) *
                       specColor * lightDiff;

    // 4. Add specular and diffuse contributions
    return vec4(diffContrib + specContrib, alpha);
}

void main () {
    vec3 kd = texture(diffuseTex, var_Color.xy).xyz;
    
    vec4 pos_world = vec4(var_Position, 1);
    //pos_world /= pos_world.w;

    vec4 positionProjected = light_VP * (vec4(lightPos, 1) + -pos_world); //* vec4(lightPos, 0);
    positionProjected = (positionProjected * 0.5) + vec4( .5, .5, .5, 0);
    
    
    float depth1 = positionProjected.z;
    vec3 kp = texture(shadowTex, positionProjected.xy).xyz;
    float newDepth = kp.z;

    if( (newDepth + 0.001) < depth1) {
        // shadow
        out_Color = vec4(ambientColor, 1) + blinn_phong(kd) - vec4(kp.x, kp.x, kp.x, 1);
    }
    else {
        // illuminated
        out_Color = vec4(ambientColor + blinn_phong(kd).xyz , 1);
    }
}
