#ifndef BLOOM_H
#define BLOOM_H

#include "../core/includes.h"
#include "../core/math.h"
#include <vector>

namespace GFX
{
	class Shader;
	class Texture;
}

struct bloomMip
{
	vec2 size;
	Vector2<int> intSize;
	GFX::Texture* texture = nullptr;
};

class bloomFBO
{
public:
	bloomFBO();
	~bloomFBO();

	bool Init(unsigned int windowWidth, unsigned int windowHeight, unsigned int mipChainLength);
	void Destroy();
	void BindForWriting();
	const std::vector<bloomMip>& MipChain() const;

private:
	bool mInit;
	unsigned int mFBO;
	std::vector<bloomMip> mMipChain;
};

class BloomRenderer
{
public:
	BloomRenderer();
	~BloomRenderer();
	bool Init(unsigned int windowWidth, unsigned int windowHeight);
	void Destroy();
	void RenderBloomTexture(unsigned int srcTexture, float filterRadius);
	GFX::Texture* BloomTexture() const;

private:
	void RenderDownsamples(unsigned int srcTexture);
	void RenderUpsamples(float filterRadius);

	bool mInit;
	bloomFBO mFBO;
	Vector2<int> mSrcViewportSize;
	vec2 mSrcViewportSizeFloat;
	GFX::Shader* mDownsampleShader;
	GFX::Shader* mUpsampleShader;
};



#endif
