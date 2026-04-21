//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
plain basic.vs plain.fs

\perturbNormal

// From https://github.com/glslify/glsl-perturb-normal/blob/master/cotangent-frame.glsl
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	// solve the linear system
	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame 
	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

// assume N, the interpolated vertex normal and 
// WP the world position
vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;



//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}


\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}

\plain.fs

#version 330 core

out vec4 FragColor;

void main()
{
	FragColor = vec4(1.0);
}
\texture.fs

#version 330 core
#include "perturbNormal"
const int MAX_LIGHTS = 8;
in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec3 u_light_position[MAX_LIGHTS];
uniform vec3 u_Ia;


uniform float u_shininess;
uniform vec3 u_camera_position;
uniform float u_intensity[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];
uniform int u_light_type[MAX_LIGHTS];
uniform vec3 u_light_direction[MAX_LIGHTS];
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;
uniform int u_num_lights;
uniform vec2 u_cone_infos[MAX_LIGHTS]; // x=alpha_min, y=alpha_max
uniform sampler2D u_normal_map;
uniform sampler2D u_shadowmap;
uniform mat4 u_light_viewprojection;
out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );
	if(color.a < u_alpha_cutoff)
		discard;

	vec3 out_color = vec3(0.0);
	out_color +=  (u_Ia * color.rgb);
	
	vec3 L;
	float N_dot_L;
	vec3 R;
	float light_intensity;
	vec3 V;
	float R_dot_V;
	vec3 D;

	vec3 texture_normal = texture(u_normal_map, v_uv).xyz;
	texture_normal = (texture_normal * 2.0) - 1.0;
	texture_normal = normalize(texture_normal);
	vec3 N = perturbNormal(normalize(v_normal), v_world_position, v_uv, texture_normal);

	for(int i = 0; i < MAX_LIGHTS; i++){
		//Attenuation

		if(i < u_num_lights){
			light_intensity = u_intensity[i]/(pow(distance(u_light_position[i], v_world_position), 2.0));
			//Difuse
		
			D = normalize(u_light_direction[i]);
			if(u_light_type[i] == 3) {
				L = D;
				light_intensity = 1.0;
			} 
			else if(u_light_type[i] == 2){
				L = normalize(u_light_position[i] - v_world_position);
				if(clamp(dot(L, D), 0.0, 1.0) >= clamp(cos(u_cone_infos[i].y), 0.0, 1.0)){
					light_intensity *= (clamp(dot(L, D), 0.0, 1.0) - clamp(cos(u_cone_infos[i].y), 0.0, 1.0)) / (clamp(cos(u_cone_infos[i].x), 0.0, 1.0) - clamp(cos(u_cone_infos[i].y), 0.0, 1.0));
				}
				else{
					light_intensity = 0.0;
				}
			}
			else {
				L = normalize(u_light_position[i] - v_world_position);
			}
			N_dot_L = clamp(dot(L,N), 0.0, 1.0);
			out_color += u_light_color[i] * color.rgb *N_dot_L * light_intensity;

			//Specular
			R = normalize(reflect(-L, N));
			V = normalize(u_camera_position - v_world_position);
			R_dot_V = clamp(dot(R,V), 0.0, 1.0);
					out_color += u_light_color[i]* color.rgb * pow(R_dot_V, u_shininess) * light_intensity;

						FragColor = vec4(out_color, color.a);
					}	

					}

				// Apply shadow
				vec4 proj_pos = u_light_viewprojection * vec4(v_world_position, 1.0);
				proj_pos /= proj_pos.w;
				proj_pos = (proj_pos + 1.0) * 0.5;
				if (proj_pos.x >= 0.0 && proj_pos.x <= 1.0 && proj_pos.y >= 0.0 && proj_pos.y <= 1.0) {
					float shadow_depth = texture(u_shadowmap, proj_pos.xy).r;
					float bias = 0.005;
					if (proj_pos.z - bias > shadow_depth)
						FragColor.rgb *= 0.3;
				}
			}


\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}


\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}
