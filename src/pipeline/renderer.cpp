#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"


using namespace SCN;

//some globals
GFX::Mesh sphere;
std::vector<GFX::FBO*> shadow_fbos;
float shadow_bias;
bool forward_culling = false;
bool directional_lights_only = false;

static vec3 getLightForward(Matrix44 light_model)
{
	vec3 light_pos = light_model.getTranslation();
	vec3 light_target = light_model * vec3(0.0f, 0.0f, -1.0f);
	return normalize(light_target - light_pos);
}

static void bindShadowUniforms(GFX::Shader* shader, const std::vector<GFX::FBO*>& shadow_fbos, const std::vector<Matrix44>& light_cams_viewproj, const std::vector<LightEntity*>& lights_list)
{
	const int lights_num = (int)lights_list.size();
	const int num_shadows = (int)shadow_fbos.size() < 8 ? (int)shadow_fbos.size() : 8;
	int shadow_slots[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };

	for (int i = 0; i < num_shadows; ++i)
	{
		if (shadow_fbos[i] && shadow_fbos[i]->depth_texture)
		{
			glActiveTexture(GL_TEXTURE0 + shadow_slots[i]);
			shadow_fbos[i]->depth_texture->bind();
		}
	}

	if (lights_num > 0)
	{
		shader->setUniform1Array("u_shadowmap", shadow_slots, lights_num < num_shadows ? lights_num : num_shadows);
		shader->setMatrix44Array("u_light_viewprojection", const_cast<Matrix44*>(light_cams_viewproj.data()), lights_num);

		std::vector<int> cast_shadows;
		cast_shadows.reserve(lights_num);
		for (auto* light : lights_list)
			cast_shadows.push_back(light->cast_shadows ? 1 : 0);

		shader->setUniform1Array("u_cast_shadows", cast_shadows.data(), lights_num);
	}
}

static void unbindShadowTextures(const std::vector<GFX::FBO*>& shadow_fbos)
{
	const int num_shadows = (int)shadow_fbos.size() < 8 ? (int)shadow_fbos.size() : 8;
	for (int i = 0; i < num_shadows; ++i)
	{
		if (shadow_fbos[i] && shadow_fbos[i]->depth_texture)
		{
			glActiveTexture(GL_TEXTURE0 + 8 + i);
			shadow_fbos[i]->depth_texture->unbind();
		}
	}

	glActiveTexture(GL_TEXTURE0);
}

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	multi_pass = true;
	scene = nullptr;
	skybox_cubemap = nullptr;
	ssao_samples = generateSpherePoints(64, 1.0f, true);
	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

struct sRenderable
{
	GFX::Mesh* mesh = nullptr;
	Material* material = nullptr;
	Matrix44 model;
};

std::vector<sRenderable> render_list;
std::vector<LightEntity*> lights_list;

static void setTonemapUniforms(GFX::Shader* shader)
{
	float total_intensity = 0.0f;
	float max_intensity = 0.0f;
	for (auto* light : lights_list)
	{
		total_intensity += light->intensity;
		if (light->intensity > max_intensity)
			max_intensity = light->intensity;
	}

	const float gamma = 2.2f;

	float average_lum = total_intensity / (float)lights_list.size();
	
	float lumwhite2 = max_intensity * max_intensity;

	const float tonemap_scale = 2.0f;

	shader->setUniform("u_scale", tonemap_scale);
	shader->setUniform("u_average_lum", average_lum);
	shader->setUniform("u_lumwhite2", lumwhite2);
	shader->setUniform("u_igamma", 1.0f / gamma);
}

void parseNode(Node* node){
	if (!node) {
		return;
	}

	render_list.push_back({
		.mesh = node->mesh,
		.material = node->material,
		.model = node->getGlobalMatrix()
		});

	for (Node* child : node->children) {
		parseNode(child);
	}
}

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================
	render_list.clear();
	lights_list.clear();
	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];
		
		
		if (!entity->visible) {
			continue;
		}

		if (entity->getType() == eEntityType::PREFAB){
			parseNode(&(entity->root));	
		}
		else if (entity->getType() == eEntityType::LIGHT) {
			LightEntity* l = (LightEntity*)entity;
			lights_list.push_back(l);
		}
		// Store Prefab Entitys
		// ...
		//		Store Children Prefab Entities

		// Store Lights
		// ...
	}

	
	
}

std::vector<sRenderable> opaque_list;
std::vector<sRenderable> transparent_list;
std::vector<Matrix44> light_cams_viewproj;
void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	parseSceneEntities(scene, camera);
	
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================
	

    // Partition renderables into opaque (including MASK) and transparent (BLEND)
	opaque_list.clear();
	transparent_list.clear();
	for (size_t i = 0; i < render_list.size(); i++) {
		if (!render_list[i].material)
			continue;

		if (render_list[i].material->alpha_mode == SCN::eAlphaMode::BLEND)
			transparent_list.push_back(render_list[i]);
		else
			opaque_list.push_back(render_list[i]);
	}

	// compute distances to camera and sort
	std::vector<std::pair<float, sRenderable>> opaque_pairs;
	std::vector<std::pair<float, sRenderable>> transparent_pairs;

	for (auto &r : opaque_list) {
		Vector3f pos = r.model.getTranslation();
		float dist = camera->eye.distance(pos);
		opaque_pairs.push_back(std::make_pair(dist, r));
	}
	
	for (auto &r : transparent_list) {
		Vector3f pos = r.model.getTranslation();
		float dist = camera->eye.distance(pos);
		transparent_pairs.push_back(std::make_pair(dist, r));
	}

	// Opaques: front-to-back (closest first) to help early z-rejection
	std::sort(opaque_pairs.begin(), opaque_pairs.end(), [](const std::pair<float,sRenderable>& a, const std::pair<float,sRenderable>& b){
		return a.first < b.first; // smaller distance first
	});

	// Transparents: back-to-front (furthest first) for correct blending
	std::sort(transparent_pairs.begin(), transparent_pairs.end(), [](const std::pair<float,sRenderable>& a, const std::pair<float,sRenderable>& b){
		return a.first > b.first; // larger distance first
	});

	// Ensure we have one FBO per light
	Vector2 window_size = CORE::getWindowSize();
    int num_lights = (int)lights_list.size();

    // Delete FBOs that are no longer needed
    while ((int)shadow_fbos.size() > num_lights)
    {
        delete shadow_fbos.back();
        shadow_fbos.pop_back();
    }

    // Create FBOs for new lights
    while ((int)shadow_fbos.size() < num_lights)
    {
        GFX::FBO* new_fbo = new GFX::FBO();
        new_fbo->setDepthOnly(window_size.x, window_size.y);
        shadow_fbos.push_back(new_fbo);
    }

	light_cams_viewproj.clear();
	for (int i = 0; i < (int)lights_list.size(); ++i) {
		Camera light_cam;
		mat4 light_model = lights_list[i]->root.getGlobalMatrix();
		vec3 light_pos = light_model.getTranslation();
		vec3 light_target = light_model * vec3(0.0f, 0.0f, -1.0f);
		if (lights_list[i]->light_type == DIRECTIONAL) {
			light_cam.lookAt(light_pos, light_target, vec3(0.0f, 1.0f, 0.0f));
			float half_size = lights_list[i]->area / 2.0f;
			light_cam.setOrthographic(-half_size, half_size, -half_size, half_size, lights_list[i]->near_distance, lights_list[i]->max_distance);
		} else {
			float aspect = window_size.y ? window_size.x / window_size.y : 1.0f;
			light_cam.lookAt(light_pos, light_target, vec3(0.0f, 1.0f, 0.0f));
			light_cam.setPerspective(lights_list[i]->cone_info.x, aspect, lights_list[i]->near_distance, lights_list[i]->max_distance);
		}
		light_cams_viewproj.push_back(light_cam.viewprojection_matrix);

		if (!lights_list[i]->cast_shadows)
			continue;

		shadow_fbos[i]->bind();
		glColorMask(false, false, false, false);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);

		if (forward_culling) { //Enable forward front culling(ImGui)
			glEnable(GL_CULL_FACE);
			glFrontFace(GL_CW);
		}
		
		//Only shadow map on opaque
		for (auto& p : opaque_pairs) {
			renderFBO(p.second.model, p.second.mesh, p.second.material, &light_cam);
		}

		glColorMask(true, true, true, true);
		shadow_fbos[i]->unbind();

		if (forward_culling) {
			glFrontFace(GL_CCW);
			glDisable(GL_CULL_FACE);
		}
		
	}
	
		
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

 if (!lighting_fbo || lighting_fbo->width != (int)window_size.x || lighting_fbo->height != (int)window_size.y) {
		delete lighting_fbo;
		lighting_fbo = new GFX::FBO();
		lighting_fbo->create(window_size.x, window_size.y, 3, GL_RGBA, GL_FLOAT, true);
	}

	if (multi_pass)
	{
		// Deferred path for opaque geometry.
		if (!gbuffer_fbo|| gbuffer_fbo->width  != (int)window_size.x|| gbuffer_fbo->height != (int)window_size.y)
		{
			delete gbuffer_fbo;
			gbuffer_fbo = new GFX::FBO();
			gbuffer_fbo->create(window_size.x, window_size.y, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);
		}


		if (!ssao_fbo || ssao_fbo->width != (int)window_size.x || ssao_fbo->height != (int)window_size.y)
		{
			delete ssao_fbo;
			ssao_fbo = new GFX::FBO();
			ssao_fbo->create(window_size.x, window_size.y, 1, GL_RGB, GL_UNSIGNED_BYTE, false);
		}

		gbuffer_fbo->bind();
		glEnable(GL_DEPTH_TEST);

		glDepthMask(true);
		glDisable(GL_BLEND);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		
		
		for (auto& p : opaque_pairs) {
			BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
			if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
				renderOnlyMesh(p.second.model, p.second.mesh, p.second.material);
			}
		}
		gbuffer_fbo->unbind();

		renderSSAOPass();

		gbuffer_fbo->depth_texture->copyTo(lighting_fbo->depth_texture);

		lighting_fbo->bind();
		glDisable(GL_BLEND);
		glClear(GL_COLOR_BUFFER_BIT);

		if (skybox_cubemap)
			renderSkybox(skybox_cubemap);

		glEnable(GL_DEPTH_TEST);
		glDepthMask(false);
		for (auto& p : transparent_pairs) {
			BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
			if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
				renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material, shadow_fbos);
			}
		}
		glDepthMask(true);

		renderLightingPass(shadow_fbos);
		
      lighting_fbo->unbind();
	}
	else
	{
		lighting_fbo->bind();
       glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (skybox_cubemap)
			renderSkybox(skybox_cubemap);

		// Forward path renders the HDR scene into lighting_fbo.
		glEnable(GL_DEPTH_TEST);
		glDepthMask(true);
		glDisable(GL_BLEND);
		for (auto& p : opaque_pairs) {
			BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
			if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
				renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material, shadow_fbos);
			}
		}

		if (!transparent_pairs.empty()) {
			glEnable(GL_DEPTH_TEST);
			glDepthMask(false);
			for (auto& p : transparent_pairs) {
				BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
				if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
					renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material, shadow_fbos);
				}
			}
			glDepthMask(true);
		}

		lighting_fbo->unbind();
	}

	GFX::Shader* tonemap_shader = GFX::Shader::Get("tonemap");
	if (tonemap_shader)
	{
		tonemap_shader->enable();
		setTonemapUniforms(tonemap_shader);
		lighting_fbo->color_textures[0]->toViewport(tonemap_shader);
		tonemap_shader->disable();
	}
	else {
		lighting_fbo->color_textures[0]->toViewport();
	}

	if (lighting_fbo->depth_texture)
		lighting_fbo->depth_texture->copyTo(nullptr);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
}

void Renderer::renderLightingPass(const std::vector<GFX::FBO*>& shadow_fbos)
{
	GFX::Shader* shader = GFX::Shader::Get("deferred_lighting");
	if (!shader)
		return;

	Camera* camera = Camera::current;
	Vector2 window_size = CORE::getWindowSize();
	GFX::Mesh* quad = GFX::Mesh::getQuad();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	shader->enable();

	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_inv_vp_mat", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_res_inv", vec2(1.0f / window_size.x, 1.0f / window_size.y));
	shader->setUniform("u_shadow_bias", shadow_bias);
	shader->setUniform("u_shininess", 8.0f);

	directional_lights_only = true;
	sendLightUniforms(shader, false);
	directional_lights_only = false;

	int texture_slots = 2;
	shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], texture_slots++);
	shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], texture_slots++);
	shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, texture_slots++);
	shader->setTexture("u_gbuffer_metallic_roughness", gbuffer_fbo->color_textures[2], texture_slots++);
	shader->setTexture("u_ssao_tex", ssao_fbo->color_textures[0], texture_slots++);
	bindShadowUniforms(shader, shadow_fbos, light_cams_viewproj, lights_list);

	quad->render(GL_TRIANGLES);

	unbindShadowTextures(shadow_fbos);

	shader->disable();

	GFX::Shader* volume_shader = GFX::Shader::Get("lighting");
	if (volume_shader)
	{
		volume_shader->enable();

		volume_shader->setUniform("u_camera_pos", camera->eye);
		volume_shader->setUniform("u_inv_vp_mat", camera->inverse_viewprojection_matrix);
		volume_shader->setUniform("u_res_inv", vec2(1.0f / window_size.x, 1.0f / window_size.y));
		volume_shader->setUniform("u_shadow_bias", shadow_bias);
		volume_shader->setUniform("u_shininess", 8.0f);

		int volume_texture_slots = 2;
		volume_shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], volume_texture_slots++);
		volume_shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], volume_texture_slots++);
		volume_shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, volume_texture_slots++);
		volume_shader->setTexture("u_gbuffer_metallic_roughness", gbuffer_fbo->color_textures[2], volume_texture_slots++);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GREATER);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glFrontFace(GL_CW);
	
		sendLightUniforms(volume_shader, true);

		glFrontFace(GL_CCW);
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
		volume_shader->disable();
	}

	glEnable(GL_DEPTH_TEST);
}

void Renderer::renderSSAOPass()
{
	if (!ssao_fbo || !gbuffer_fbo || !gbuffer_fbo->depth_texture)
		return;

	GFX::Shader* ao_shader = GFX::Shader::Get("ssao");
	if (!ao_shader)
		return;

	Camera* camera = Camera::current;
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	Matrix44 proj = camera->projection_matrix;
	Matrix44 proj_inv = proj;
	proj_inv.inverse();

	ssao_fbo->bind();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	ao_shader->enable();
	ao_shader->setUniform("u_p_mat", proj);
	ao_shader->setUniform("u_inv_p_mat", proj_inv);
	ao_shader->setUniform("u_view_mat", camera->view_matrix);
	ao_shader->setUniform("u_res_inv", vec2(1.0f / ssao_fbo->color_textures[0]->width, 1.0f / ssao_fbo->color_textures[0]->height));
	ao_shader->setUniform("u_sample_count", ssao_sample_count);
	ao_shader->setUniform("u_sample_radius", ssao_radius);
	if (!ssao_samples.empty())
		ao_shader->setUniform3Array("u_sample_pos", (float*)&ssao_samples[0], ssao_sample_count);
	ao_shader->setTexture("u_depth_tex", gbuffer_fbo->depth_texture, 7);
	ao_shader->setTexture("u_normal_tex", gbuffer_fbo->color_textures[1], 8);

	quad->render(GL_TRIANGLES);

	ao_shader->disable();
	ssao_fbo->unbind();

	glEnable(GL_DEPTH_TEST);
}

void Renderer::renderFBO(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* light_cam) { //Create a simple shader to render the shadowmaps on the texture.
	GFX::Shader* shader = GFX::Shader::Get("plain");
	if (!shader)
		return;
	shader->enable();
	if (material)
		material->bind(shader);
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", light_cam->viewprojection_matrix);
	
	mesh->render(GL_TRIANGLES);
	shader->disable();

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	// Apply skybox necesarry config:
	// No blending, no dpeth test, we are always rendering the skybox
	// Set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	// Center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	shader->setUniform("u_texture", cubemap, 5);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}



void Renderer::renderOnlyMesh(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);
	glDepthMask(true);

	//chose a shader
	shader = GFX::Shader::Get("material");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);

	float t = getTime();
	shader->setUniform("u_time", t);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, const std::vector<GFX::FBO*>& shadow_fbos)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);
	glDepthMask(material->alpha_mode == SCN::eAlphaMode::BLEND ? GL_FALSE : GL_TRUE);

	//chose a shader

	shader = GFX::Shader::Get("forward_transparent");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t );
	sendLightUniforms(shader, false);

	shader->setUniform("u_shadow_bias", shadow_bias); //ImGui value
	bindShadowUniforms(shader, shadow_fbos, light_cams_viewproj, lights_list);
	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	// Unbind shadow map textures to avoid state leakage into subsequent renders
	unbindShadowTextures(shadow_fbos);

	//disable shader
	shader->disable();
	
	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthMask(true);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

void Renderer::sendLightUniforms(GFX::Shader *shader, bool is_volume) {
	int lights_num = (int)lights_list.size();
	
	if (is_volume)
	{
		Camera* camera = Camera::current;
		shader->setUniform("u_Ia", vec3(0.0f, 0.0f, 0.0f));
		int light_index = 0;
		for (auto& p : lights_list) {
			Matrix44 light_model = p->root.getGlobalMatrix();
			if (p->light_type == DIRECTIONAL)
			{
				++light_index;
				continue;
			}

			Vector3f light_color[1] = { p->color };
			float light_intensity[1] = { p->intensity };
			Vector3f light_position[1] = { light_model.getTranslation() };
			int light_type[1] = { (int)p->light_type };
			Vector3f light_direction[1] = { getLightForward(light_model) };
			Vector2f cone_info[1] = { vec2(p->cone_info.x * DEG2RAD, p->cone_info.y * DEG2RAD) };

			shader->setUniform("u_num_lights", 1);
			shader->setUniform3Array("u_light_color", (float*)light_color, 1);
			shader->setUniform1Array("u_intensity", light_intensity, 1);
			shader->setUniform3Array("u_light_position", (float*)light_position, 1);
			shader->setUniform1Array("u_light_type", light_type, 1);
			shader->setUniform3Array("u_light_direction", (float*)light_direction, 1);
			shader->setUniform2Array("u_cone_infos", (float*)cone_info, 1);

			int shadow_slot[1] = { 8 };
			int cast_shadows[1] = { p->cast_shadows ? 1 : 0 };
			Matrix44 light_viewproj[1] = { light_cams_viewproj[light_index] };
			if (p->cast_shadows && light_index < (int)shadow_fbos.size() && shadow_fbos[light_index] && shadow_fbos[light_index]->depth_texture)
			{
				glActiveTexture(GL_TEXTURE0 + shadow_slot[0]);
				shadow_fbos[light_index]->depth_texture->bind();
			}
			shader->setUniform1Array("u_shadowmap", shadow_slot, 1);
			shader->setMatrix44Array("u_light_viewprojection", light_viewproj, 1);
			shader->setUniform1Array("u_cast_shadows", cast_shadows, 1);

			Matrix44 volume_model;
			vec3 light_pos = light_model.getTranslation();
			volume_model.setTranslation(light_pos.x, light_pos.y, light_pos.z);
			volume_model.scale(p->max_distance, p->max_distance, p->max_distance);
			shader->setUniform("u_model", volume_model);
			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

			sphere.render(GL_TRIANGLES);

			if (p->cast_shadows && light_index < (int)shadow_fbos.size() && shadow_fbos[light_index] && shadow_fbos[light_index]->depth_texture)
			{
				glActiveTexture(GL_TEXTURE0 + shadow_slot[0]);
				shadow_fbos[light_index]->depth_texture->unbind();
				glActiveTexture(GL_TEXTURE0);
			}

			++light_index;
		}
		return;
	}

	shader->setUniform("u_Ia", scene->ambient_light);
	shader->setUniform("u_num_lights", lights_num);

	// Clear light vectors before filling
	std::vector<Vector3f> light_colors;
	std::vector<float> light_intensities;
	std::vector<Vector3f> light_positions;
	std::vector<int> light_types;
	std::vector<Vector3f> light_directions;
	std::vector<Vector2f> cone_infos;
		std::vector<Matrix44> light_models;

	for (auto& p : lights_list) {
		Matrix44 light_model = p->root.getGlobalMatrix();
		light_models.push_back(light_model);
		light_colors.push_back(p->color);
		light_positions.push_back(light_model.getTranslation());
		light_intensities.push_back(directional_lights_only && p->light_type != DIRECTIONAL ? 0.0f : p->intensity);
		light_types.push_back(p->light_type);
		light_directions.push_back(getLightForward(light_model));
		cone_infos.push_back(vec2(p->cone_info.x * DEG2RAD, p->cone_info.y * DEG2RAD));
	}
	

	shader->setMatrix44Array("u_light_models", light_models.data(), lights_num);
	shader->setUniform3Array("u_light_color", (float*)light_colors.data(), lights_num);
	shader->setUniform1Array("u_intensity", light_intensities.data(), lights_num);
	shader->setUniform3Array("u_light_position", (float*)light_positions.data(), lights_num);
	shader->setUniform1Array("u_light_type", light_types.data(), lights_num);
	shader->setUniform3Array("u_light_direction", (float*)light_directions.data(), lights_num);
	shader->setUniform2Array("u_cone_infos", (float*)cone_infos.data(), lights_num);

	
	
	
}
#ifndef SKIP_IMGUI

void Renderer::showUI()
{
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);
	
	//add here your stuff
	//...
	//EDITOR.cpp LINE 323 -> SliderFloat for shininess
	ImGui::Checkbox("Use Deferred Pipeline", &multi_pass);
	ImGui::SliderFloat("Shadow_bias", &shadow_bias,0.00001f, 0.1f);
	ImGui::Checkbox("Forward Face Culling", &forward_culling);
}

#else
void Renderer::showUI() {}
#endif
