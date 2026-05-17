#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"

//forward declarations
class Camera;
class Skeleton;
namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}

namespace SCN {

	class Prefab;
	class Material;

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;
		bool multi_pass;

		GFX::Texture* skybox_cubemap;
		SCN::Scene* scene;
		std::vector<Vector3f> ssao_samples;

		GFX::FBO* gbuffer_fbo = nullptr; // persistent GBuffer FBO, reused every frame
		GFX::FBO* lighting_fbo = nullptr;
		GFX::FBO* ssao_fbo = nullptr;
		//updated every frame
		Renderer(const char* shaders_atlas_filename );
		void sendLightUniforms(GFX::Shader *shader, bool is_volume);
		void renderDeferredLightingPass();
		//just to be sure we have everything ready for the rendering
		void setupScene();
		void renderOnlyMesh(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		//add here your functions
		//...
		void renderFBO(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* light_cam);
		void parseSceneEntities(SCN::Scene* scene, Camera* camera);
		void renderSSAOPass();
		void renderLightingPass(const std::vector<GFX::FBO*>& shadow_fbos);
		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, const std::vector<GFX::FBO*>& shadow_fbos = {});

		void showUI();
	};

};
