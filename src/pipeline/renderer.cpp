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

	
	//render skybox
	if(skybox_cubemap)
		renderSkybox(skybox_cubemap);

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

	// Render opaque objects
	for (auto &p : opaque_pairs)
		if (camera->testBoxInFrustum(p.second.mesh->box.center, p.second.mesh->box.halfsize))
			renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material);

	// Render transparent objects with depth writes disabled (but depth test still enabled)
	for (auto &p : transparent_pairs)
		if (camera->testBoxInFrustum(p.second.mesh->box.center, p.second.mesh->box.halfsize))
			renderMeshWithMaterial(p.second.model, p.second.mesh, p.second.material);
	
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

	shader->setUniform("u_texture", cubemap, 0);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}




// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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
	shader->enable();
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_Ia", scene->ambient_light);
	shader->setUniform("u_num_lights", (int)lights_list.size());

	// Clear light vectors before filling
	std::vector<Vector3f> light_colors;
	std::vector<Vector3f> light_intensities;
	std::vector<Vector3f> light_positions;
	std::vector<int> light_types;
	std::vector<Vector3f> light_directions;

	for (auto& p : lights_list) {
		light_colors.push_back(p->color);
		light_positions.push_back(p->root.model.getTranslation());
		light_intensities.push_back(p->intensity);
		light_types.push_back(p->light_type);
		light_directions.push_back(p->root.model.frontVector());
	}
	
	shader->setUniform3Array("u_light_color", (float*)light_colors.data(), (int)lights_list.size());
	shader->setUniform1Array("u_intensity", (float*)light_intensities.data(), (int)lights_list.size());
	shader->setUniform3Array("u_light_position", (float*)light_positions.data(), (int)lights_list.size());
	shader->setUniform1Array("u_light_type", light_types.data(), (int)lights_list.size());
	shader->setUniform3Array("u_light_direction", (float*)light_directions.data(), (int)lights_list.size());

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();
	
	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	//add here your stuff
	//...
}

#else
void Renderer::showUI() {}
#endif