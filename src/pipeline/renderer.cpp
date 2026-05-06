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
Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;
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
			PrefabEntity* e = (PrefabEntity*)entity;
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
		if (lights_list[i]->light_type == DIRECTIONAL) {
			light_cam.lookAt(light_pos, light_model * vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
			float half_size = lights_list[i]->area / 2.0f;
			light_cam.setOrthographic(-half_size, half_size, -half_size, half_size, lights_list[i]->near_distance, lights_list[i]->max_distance);
		} else {
			float aspect = window_size.x / window_size.y;
			light_cam.lookAt(light_pos, light_model * vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
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

	//render skybox
	if (skybox_cubemap)
		renderSkybox(skybox_cubemap);

	// Render opaque objects — reuse geometry_fbo, only recreate if size changed
	if (!gbuffer_fbo|| gbuffer_fbo->width  != (int)window_size.x|| gbuffer_fbo->height != (int)window_size.y)
	{
		delete gbuffer_fbo;
		gbuffer_fbo = new GFX::FBO();
		gbuffer_fbo->create(window_size.x, window_size.y, 2, GL_RGBA, GL_UNSIGNED_BYTE, true);
	}

	gbuffer_fbo->bind();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	for (auto& p : opaque_pairs) {
		BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
		if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
			renderOnlyMesh(p.second.model, p.second.mesh, p.second.material);
		}
	}
	gbuffer_fbo->unbind();

	for (auto& p : opaque_pairs) {
		BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
		if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
			renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material, shadow_fbos);
		}
	}


	
	// Render transparent objects
	for (auto& p : transparent_pairs) {
		BoundingBox mesh_box = transformBoundingBox(p.second.model, p.second.mesh->box);
		if (camera->testBoxInFrustum(mesh_box.center, mesh_box.halfsize)) {
			renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material, shadow_fbos);
		}
	}
}

void Renderer::renderDeferredLightingPass(const std::vector<GFX::FBO*>& shadow_fbos)
{
	Camera* camera = Camera::current;
	

	GFX::Shader* shader = GFX::Shader::Get("deferred");
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	quad->render(GL_TRIANGLES);

	glEnable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE0);

	if (!shader)
		return;
	shader->enable();
	
	

	
	shader->disable();
}

void Renderer::renderFBO(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* light_cam) { //Create a simple shader to render the shadowmaps on the texture.
	GFX::Shader* shader = GFX::Shader::Get("plain");
	if (!shader)
		return;
	shader->enable();
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", light_cam->viewprojection_matrix);
	mesh->render(GL_TRIANGLES);
	shader->disable();
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

	//chose a shader

	shader = GFX::Shader::Get("texture");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	shader->enable();
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_shininess", 40.0f); // or expose as a global like shadow_bias

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t );

	Vector2 window_size = CORE::getWindowSize();
	shader->setUniform("u_res_inv", vec2(1.0f / window_size.x, 1.0f / window_size.y));

	shader->setUniform("u_inv_vp_mat", camera->inverse_viewprojection_matrix); // for deferred
	sendLightUniforms(shader);

	int texture_slots = 1;
	shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], texture_slots++);
	shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], texture_slots++);
	shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, texture_slots++);

	
	int lights_num = (int)lights_list.size();
	shader->setUniform("u_shadow_bias", shadow_bias); //ImGui value
	// Upload shadow maps and light view-projection matrices
	{
		int num_shadows = (int)shadow_fbos.size() < 8 ? (int)shadow_fbos.size() : 8;
		// Bind each shadow map depth texture to a texture unit (starting at slot 8)
		int shadow_slots[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };
		for (int i = 0; i < num_shadows; ++i) {
			if (shadow_fbos[i] && shadow_fbos[i]->depth_texture) {
				glActiveTexture(GL_TEXTURE0 + shadow_slots[i]);
				shadow_fbos[i]->depth_texture->bind();
			}
		}
		shader->setUniform1Array("u_shadowmap", shadow_slots, lights_num < num_shadows ? lights_num : num_shadows);

		// Upload light view-projection matrices
		shader->setMatrix44Array("u_light_viewprojection", light_cams_viewproj.data(), lights_num);

		// Upload shadow casting flags
		std::vector<int> cast_shadows;
		for (auto& light : lights_list) {
			cast_shadows.push_back(light->cast_shadows ? 1 : 0);
		}
		shader->setUniform1Array("u_cast_shadows", cast_shadows.data(), lights_num);
	}
	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	// Unbind shadow map textures to avoid state leakage into subsequent renders
	{
		int num_shadows = (int)shadow_fbos.size() < 8 ? (int)shadow_fbos.size() : 8;
		for (int i = 0; i < num_shadows; ++i) {
			if (shadow_fbos[i] && shadow_fbos[i]->depth_texture) {
				glActiveTexture(GL_TEXTURE0 + 8 + i);
				shadow_fbos[i]->depth_texture->unbind();
			}
		}
		glActiveTexture(GL_TEXTURE0); // restore default active unit
	}

	//disable shader
	quad->render(GL_TRIANGLES);
	shader->disable();
	
	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

void Renderer::sendLightUniforms(GFX::Shader *shader) {
	int lights_num = (int)lights_list.size();

	shader->setUniform("u_Ia", scene->ambient_light);
	shader->setUniform("u_num_lights", lights_num);

	// Clear light vectors before filling
	std::vector<Vector3f> light_colors;
	std::vector<float> light_intensities;
	std::vector<Vector3f> light_positions;
	std::vector<int> light_types;
	std::vector<Vector3f> light_directions;
	std::vector<Vector2f> cone_infos;

	for (auto& p : lights_list) {
		light_colors.push_back(p->color);
		light_positions.push_back(p->root.model.getTranslation());
		light_intensities.push_back(p->intensity);
		light_types.push_back(p->light_type);
		light_directions.push_back(p->root.model.frontVector());
		cone_infos.push_back(vec2(p->cone_info.x * DEG2RAD, p->cone_info.y * DEG2RAD));
	}
	
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
	ImGui::Checkbox("Multi pass", &multi_pass);
	ImGui::SliderFloat("Shadow_bias", &shadow_bias,0.00001f, 0.1f);
	ImGui::Checkbox("Forward Face Culling", &forward_culling);
}

#else
void Renderer::showUI() {}
#endif