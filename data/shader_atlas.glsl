//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs forward_transparent.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
plain basic.vs plain.fs
deferred quad.vs deferred.fs
material basic.vs material.fs
lighting basic.vs deferred_lighting.fs
deferred_lighting quad.vs deferred_lighting.fs
forward_transparent basic.vs forward_transparent.fs
ssao quad.vs ssao.fs
tonemap quad.vs tonemap.fs

\gamma_functions
vec3 degamma(vec3 color)
{
	return pow(color, vec3(2.2));
}

vec3 gamma(vec3 color)
{
	return pow(color, vec3(1.0/2.2));
}

\PBR_functions
#define PI 3.14159265359
vec3 fresnel(vec3 V, vec3 H, vec3 F0){
	float HdotV = clamp(dot(H, V), 0.0, 1.0);
	float correction = 0;
	if(HdotV <= 0.0)
		correction = 0.0001;
	return F0 + (1.0 - F0) * pow(1.0 - HdotV + correction, 5.0);
}

float D_GGX(vec3 H, vec3 N, float roughness)
{
	float alpha = roughness * roughness;
	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float correction = 0;
	if (NdotH <= 0.0)
		correction = 0.0001;
	return pow(alpha, 2.0)/(PI * pow((pow(NdotH, 2.0) + correction) * (pow(alpha, 2.0) - 1.0) + 1.0 , 2.0));
}

float G_Schlick_Smith(vec3 V, vec3 N, float roughness)
{
	float alpha = roughness * roughness;
	float k = alpha / 2.0;
	float NdotV = clamp(dot(N, V), 0.0, 1.0);
	float correction = 0;
	if (NdotV <= 0.0)
		correction = 0.0001;
	return NdotV / ((NdotV + correction) * (1.0 - k) + k);
}

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
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	mat3 normal_matrix = mat3(transpose(inverse(u_model)));
	v_normal = normalize(normal_matrix * a_normal);
	
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
in vec3 a_normal;
out vec2 v_uv;
out vec4 v_color;
out vec3 v_normal;
in vec4 a_color;



void main()
{	
	v_color = a_color;
	gl_Position = vec4( a_vertex, 1.0 );
	v_uv = a_coord;
	v_normal = a_normal;
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
in vec2 v_uv;
out vec4 FragColor;
uniform mat4 u_model;
uniform mat4 u_viewprojection;
uniform vec4 u_color;
uniform float u_alpha_cutoff;
uniform sampler2D u_texture;

void main()
{
	vec4 color = u_color * texture(u_texture, v_uv);
	if(color.a < u_alpha_cutoff)
		discard;
	
	FragColor = vec4(0.0, 0.0, 0.0, 1.0);

	
}

\material.fs

#version 330 core
#include "perturbNormal"
#include "gamma_functions"
in vec2 v_uv;
in vec3 v_normal;
in vec3 v_world_position;

uniform sampler2D u_normal_map;
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_alpha_cutoff;
uniform int u_has_normal_map;
uniform sampler2D u_metallic_roughness;
uniform float u_roughness_factor;
uniform float u_metallic_factor;

out vec4 FragColor;
layout(location = 0) out vec4 gbuffer_albedo;
layout(location = 1) out vec4 gbuffer_normal_mat;
layout(location = 2) out vec4 gbuffer_metallic_roughness;


void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color.xyz = degamma(color.xyz);
	vec3 N;

	
	if(u_has_normal_map == 1){
	vec3 texture_normal = texture(u_normal_map, uv).xyz;
	
	texture_normal = (texture_normal * 2.0) - 1.0;
	
		N = perturbNormal(normalize(v_normal), v_world_position, uv, normalize(texture_normal));
	}
	else{
		N = normalize(v_normal);
	}
	color *= texture( u_texture, uv );
	
	if(color.a < u_alpha_cutoff)
		discard;
	vec4 mr_sample = texture(u_metallic_roughness, uv);
	float metallic = clamp(mr_sample.b * u_metallic_factor, 0.0, 1.0);
	float roughness = clamp(mr_sample.g * u_roughness_factor, 0.04, 1.0);

	gbuffer_albedo = color;
	gbuffer_normal_mat = vec4(N.x * 0.5 + 0.5, N.y * 0.5 + 0.5, N.z * 0.5 + 0.5, 0.0);
	gbuffer_metallic_roughness = vec4(metallic, roughness, 0.0, color.a);
}


\forward_transparent.fs

#version 330 core
#include "perturbNormal"
#include "PBR_functions"
#include "gamma_functions"
#define PI 3.14159265359
const int MAX_LIGHTS = 8;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec3 u_light_position[MAX_LIGHTS];
uniform vec3 u_Ia;

uniform float u_roughness_factor;
uniform float u_metallic_factor;
uniform sampler2D u_metallic_roughness;

uniform float u_shininess;
uniform vec3 u_camera_pos;
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
uniform int u_has_normal_map;
uniform sampler2D u_shadowmap[MAX_LIGHTS];
uniform mat4 u_light_viewprojection[MAX_LIGHTS];
uniform int u_cast_shadows[MAX_LIGHTS];
uniform float u_shadow_bias;


out vec4 FragColor;

float computeShadowFactor(int light_index, vec3 world_pos)
{
	if(u_cast_shadows[light_index] == 0)
		return 1.0;

	vec4 proj_pos = u_light_viewprojection[light_index] * vec4(world_pos, 1.0);
	proj_pos /= proj_pos.w;
	proj_pos = (proj_pos + 1.0) * 0.5;
	if (proj_pos.x < 0.0 || proj_pos.x > 1.0 || proj_pos.y < 0.0 || proj_pos.y > 1.0 || proj_pos.z < 0.0 || proj_pos.z > 1.0)
		return 1.0;

	float shadow_depth = texture(u_shadowmap[light_index], proj_pos.xy).r;
	float real_depth = proj_pos.z - u_shadow_bias;
	return real_depth > shadow_depth ? 0.0 : 1.0;
}

void main()
{
	vec3 world_pos = v_world_position;
	vec2 uv = v_uv;
	vec4 color = u_color * texture( u_texture, uv );
	color.xyz = degamma(color.xyz);
	if(color.a <0.9 && floor(mod(gl_FragCoord.x, 2.0)) != floor(mod(gl_FragCoord.y, 2.0)))
		discard;
	if(color.a < u_alpha_cutoff)
		discard;
	
	vec3 N = normalize(v_normal);
	if(u_has_normal_map == 1){
		vec3 texture_normal = texture(u_normal_map, uv).xyz;
		texture_normal = (texture_normal * 2.0) - 1.0;
		N = perturbNormal(normalize(v_normal), v_world_position, uv, normalize(texture_normal));
	}

	vec4 metallic_roughness = texture(u_metallic_roughness, uv);
	float metallic = clamp(metallic_roughness.b * u_metallic_factor, 0.0, 1.0);
	float roughness = clamp(metallic_roughness.g * u_roughness_factor, 0.04, 1.0);

	vec3 ambient_light = degamma(u_Ia);
	vec3 out_color = ambient_light * color.rgb;
	vec3 V = normalize(u_camera_pos - v_world_position);
	vec3 L;
	vec3 R;
	vec3 H;
	float light_intensity;
	float R_dot_V;
	vec3 light_forward;
	float N_dot_L;

	for(int i = 0; i < MAX_LIGHTS && i < u_num_lights; ++i){
		light_forward = normalize(u_light_direction[i]);
		if(u_light_type[i] == 3) {
			L = normalize(-light_forward);
			light_intensity = u_intensity[i];
		}
		else {
			vec3 to_light = u_light_position[i] - v_world_position;
			float dist_sq = max(dot(to_light, to_light), 0.0001);
			L = normalize(to_light);
			light_intensity = u_intensity[i] / dist_sq;

			if(u_light_type[i] == 2){
				float cone_angle = dot(normalize(v_world_position - u_light_position[i]), light_forward);
				float outer_cos = cos(u_cone_infos[i].y);
				float inner_cos = cos(u_cone_infos[i].x);
				if(cone_angle >= outer_cos){
					float cone_den = max(inner_cos - outer_cos, 0.0001);
					light_intensity *= clamp((cone_angle - outer_cos) / cone_den, 0.0, 1.0);
				}
				else{
					light_intensity = 0.0;
				}
			}
		}

		N_dot_L = clamp(dot(L, N), 0.0, 1.0);
		if (N_dot_L <= 0.0 || light_intensity <= 0.0)
			continue;

		H = normalize(L + V);
		vec3 F0 = mix(vec3(0.04), color.rgb, metallic);
		vec3 F = fresnel(V, H, F0);
		float D = D_GGX(H, N, roughness);
		float G1 = G_Schlick_Smith(V, N, roughness);
		float G2 = G_Schlick_Smith(L, N, roughness);
		float G = G1 * G2;
		vec3 diffuse = (1.0-metallic) * color.rgb / PI;
		float N_dot_V = clamp(dot(N, V), 0.0, 1.0);

		vec3 specular = (F * D * G) / (4 * N_dot_V * N_dot_L + 0.0001);
		vec3 light_color = degamma(u_light_color[i]);
		
		out_color += (diffuse + specular) * light_intensity * light_color * N_dot_L * computeShadowFactor(i, world_pos);
		}
	out_color = gamma(out_color);
	FragColor = vec4(out_color, color.a);
}

\deferred_lighting.fs

#version 330 core
#include "PBR_functions"
#include "gamma_functions"
#define PI 3.14159265359
const int MAX_LIGHTS = 8;

in vec2 v_uv;


uniform vec3 u_light_position[MAX_LIGHTS];
uniform vec3 u_Ia;
uniform float u_shininess;
uniform vec3 u_camera_pos;
uniform float u_intensity[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];
uniform int u_light_type[MAX_LIGHTS];
uniform vec3 u_light_direction[MAX_LIGHTS];
uniform float u_alpha_cutoff;
uniform int u_num_lights;
uniform vec2 u_cone_infos[MAX_LIGHTS];
uniform sampler2D u_shadowmap[MAX_LIGHTS];
uniform mat4 u_light_viewprojection[MAX_LIGHTS];
uniform int u_cast_shadows[MAX_LIGHTS];
uniform float u_shadow_bias;

uniform sampler2D u_gbuffer_depth;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_metallic_roughness;
uniform sampler2D u_ssao_tex;
uniform mat4 u_inv_vp_mat;
uniform vec2 u_res_inv;

out vec4 FragColor;

float computeShadowFactor(int light_index, vec3 world_pos)
{
	if(u_cast_shadows[light_index] == 0)
		return 1.0;

	vec4 proj_pos = u_light_viewprojection[light_index] * vec4(world_pos, 1.0);
	proj_pos /= proj_pos.w;
	proj_pos = (proj_pos + 1.0) * 0.5;
	if (proj_pos.x < 0.0 || proj_pos.x > 1.0 || proj_pos.y < 0.0 || proj_pos.y > 1.0 || proj_pos.z < 0.0 || proj_pos.z > 1.0)
		return 1.0;

	float shadow_depth = texture(u_shadowmap[light_index], proj_pos.xy).r;
	float real_depth = proj_pos.z - u_shadow_bias;
	return real_depth > shadow_depth ? 0.0 : 1.0;
}

void main()
{
	vec2 uv = gl_FragCoord.xy * u_res_inv;
	float depth = texture(u_gbuffer_depth, uv).r;
	vec4 color = texture(u_gbuffer_color, uv);
	vec4 normal_mat = texture(u_gbuffer_normal, uv);

    if(normal_mat.a > 0.5)
    {
		FragColor = vec4(color.xyz, 1.0);
        return;
    }

	
	if(depth >= 1.0)
		discard;

	float ao = texture(u_ssao_tex, uv).r;
	float depth_clip = depth * 2.0 - 1.0;
	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);
	vec4 not_norm_world_pos = u_inv_vp_mat * clip_coords;
	vec3 world_pos = not_norm_world_pos.xyz / not_norm_world_pos.w;

	vec3 N = normalize(texture(u_gbuffer_normal, uv).xyz * 2.0 - 1.0);
	vec3 ambient_light = degamma(u_Ia);
	vec3 out_color = ambient_light * color.rgb * ao;
	vec3 V = normalize(u_camera_pos - world_pos);

	vec4 metallic_roughness = texture(u_gbuffer_metallic_roughness, uv);
	float metallic = clamp(metallic_roughness.r, 0.0, 1.0);
	float roughness = clamp(metallic_roughness.g, 0.04, 1.0);
	
	vec3 H;
	vec3 L;
	vec3 R;
	vec3 light_forward;
	float light_intensity;
	float N_dot_L;
	float R_dot_V;

	for(int i = 0; i < MAX_LIGHTS && i < u_num_lights; ++i){
		light_forward = normalize(u_light_direction[i]);
		if(u_light_type[i] == 3) {
			L = normalize(-light_forward);
			light_intensity = u_intensity[i];
		}
		else {
			vec3 to_light = u_light_position[i] - world_pos;
			float dist_sq = max(dot(to_light, to_light), 0.0001);
			L = normalize(to_light);
			light_intensity = u_intensity[i] / dist_sq;

			if(u_light_type[i] == 2){
				float cone_angle = dot(normalize(world_pos - u_light_position[i]), light_forward);
				float outer_cos = cos(u_cone_infos[i].y);
				float inner_cos = cos(u_cone_infos[i].x);
				if(cone_angle >= outer_cos){
					float cone_den = max(inner_cos - outer_cos, 0.0001);
					light_intensity *= clamp((cone_angle - outer_cos) / cone_den, 0.0, 1.0);
				}
				else{
					light_intensity = 0.0;
				}
			}
		}
		N_dot_L = clamp(dot(L, N), 0.0, 1.0);
		if (N_dot_L <= 0.0 || light_intensity <= 0.0)
			continue;

		H = normalize(L + V);
		vec3 F0 = mix(vec3(0.04), color.rgb, metallic);
		vec3 F = fresnel(V, H, F0);
		float D = D_GGX(H, N, roughness);
		float G1 = G_Schlick_Smith(V, N, roughness);
		float G2 = G_Schlick_Smith(L, N, roughness);
		float G = G1 * G2;
		vec3 diffuse = (1.0-metallic) * color.rgb / PI;
		float N_dot_V = clamp(dot(N, V), 0.0, 1.0);

		vec3 specular = (F * D * G) / (4 * N_dot_V * N_dot_L + 0.0001);
		vec3 light_color = degamma(u_light_color[i]);
		
		out_color += (diffuse + specular) * light_intensity * light_color * N_dot_L * computeShadowFactor(i, world_pos);
		
		}
	FragColor = vec4(out_color, color.a);
}

\ssao.fs

#version 330 core

in vec2 v_uv;

uniform sampler2D u_depth_tex;
uniform sampler2D u_normal_tex;
uniform mat4 u_p_mat;
uniform mat4 u_inv_p_mat;
uniform mat4 u_view_mat;
uniform vec2 u_res_inv;
uniform int u_sample_count;
uniform float u_sample_radius;
uniform vec3 u_sample_pos[64];

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv + 0.5 * u_res_inv;
	float depth = texture(u_depth_tex, uv).r;

	if(depth >= 1.0)
	{
		FragColor = vec4(1.0);
		return;
	}

	vec4 clip_coords = vec4(uv, depth, 1.0);
	clip_coords.xyz = clip_coords.xyz * 2.0 - 1.0;

	vec4 view_sample_origin = u_inv_p_mat * clip_coords;
	view_sample_origin /= view_sample_origin.w;
	vec3 N = normalize(texture(u_normal_tex, uv).xyz * 2.0 - 1.0);
	N = normalize((u_view_mat * vec4(N, 0.0)).xyz);

	vec3 v = vec3(0.0, 1.0, 0.0);
	vec3 T = normalize(v - N * dot(v, N));
	vec3 B = cross(N, T);
	mat3 rotmat = mat3(T, B, N);

	float ao_term = 0.0;
	for (int i = 0; i < u_sample_count; ++i)
	{
		vec3 view_sample = rotmat * u_sample_pos[i];
		view_sample *= u_sample_radius;
		view_sample += view_sample_origin.xyz;

		vec4 proj_sample = u_p_mat * vec4(view_sample, 1.0);
		proj_sample /= proj_sample.w;
		proj_sample.xyz = proj_sample.xyz * 0.5 + 0.5;

		vec2 sample_uv = proj_sample.xy;
		if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0)
		{
			ao_term += 1.0;
			continue;
		}

		float sample_depth = texture(u_depth_tex, sample_uv).r;
		if (sample_depth >= proj_sample.z)
			ao_term += 1.0;
	}

	ao_term /= float(u_sample_count);
	FragColor = vec4(vec3(ao_term), 1.0);
}

\tonemap.fs

#version 330 core

in vec2 v_uv;

uniform sampler2D u_texture;
uniform float u_scale;
uniform float u_average_lum;
uniform float u_lumwhite2;
uniform float u_igamma;

out vec4 FragColor;

void main()
{
	vec4 color = texture(u_texture, v_uv);
	vec3 rgb = color.xyz;

	float lum = max(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0001);
	float average_lum = max(u_average_lum, 0.0001);
	float lumwhite2 = max(u_lumwhite2, 0.0001);

	float L = (u_scale / average_lum) * lum;
	float Ld = (L * (1.0 + L / lumwhite2)) / (1.0 + L);

	rgb = (rgb / lum) * Ld;
	rgb = max(rgb, vec3(0.001));
	rgb = pow(rgb, vec3(u_igamma));

	FragColor = vec4(rgb, color.a);
}

\deferred.fs
 
#version 330 core

in vec2 v_uv;
in vec4 v_color;
in vec3 v_normal; 


layout(location = 0) out vec4 gbuffer_albedo;
layout(location = 1) out vec4 gbuffer_normal_mat;

out vec4 FragColor;

void main()
{
   	vec3 N = normalize(v_normal);
    gbuffer_albedo = vec4(v_color.rgb, v_color.a);
	gbuffer_normal_mat = vec4(N.x * 0.5 + 0.5, N.y * 0.5 + 0.5, N.z * 0.5 + 0.5, 0.0);
	
    
}

\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

layout(location = 0) out vec4 gbuffer_albedo;
layout(location = 1) out vec4 gbuffer_normal_mat;

void main()
{	
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	vec3 N = normalize(v_normal);
	FragColor = color;
	gbuffer_albedo = vec4(color.rgb, 1.0);
	gbuffer_normal_mat = vec4(0.0,0.0,0.0, 1.0);

	
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
	//
	NormalColor = vec4(N * 0.5 + 0.5, 1.0);
}


\depth.fs

#version 330 core
const int MAX_LIGHTS = 8;
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
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	mat3 normal_matrix = mat3(transpose(inverse(u_model)));
	v_normal = normalize(normal_matrix * a_normal);
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}
