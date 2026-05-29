#include "bloom.h"

#include <algorithm>
#include <climits>
#include <iostream>

#include "mesh.h"
#include "shader.h"

bloomFBO::bloomFBO() : mInit(false), mFBO(0) {}

bloomFBO::~bloomFBO()
{
	Destroy();
}

bool bloomFBO::Init(unsigned int windowWidth, unsigned int windowHeight, unsigned int mipChainLength)
{
	Destroy();

	if (windowWidth == 0 || windowHeight == 0 || mipChainLength == 0)
		return false;

	if (windowWidth > (unsigned int)INT_MAX || windowHeight > (unsigned int)INT_MAX)
	{
		std::cerr << "Window size conversion overflow - cannot build bloom FBO!" << std::endl;
		return false;
	}

	glGenFramebuffers(1, &mFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

	Vector2<int> mipIntSize((int)windowWidth, (int)windowHeight);

	for (unsigned int i = 0; i < mipChainLength; ++i)
	{
		bloomMip mip;
		mipIntSize.x = (std::max)(1, mipIntSize.x / 2);
		mipIntSize.y = (std::max)(1, mipIntSize.y / 2);
		mip.intSize = mipIntSize;
		mip.size = vec2((float)mipIntSize.x, (float)mipIntSize.y);

		glGenTextures(1, &mip.texture);
		glBindTexture(GL_TEXTURE_2D, mip.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, mip.intSize.x, mip.intSize.y, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		mMipChain.push_back(mip);
	}

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mMipChain[0].texture, 0);

	const GLenum attachments[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, attachments);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "Bloom FBO error, status: 0x" << std::hex << status << std::dec << std::endl;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		Destroy();
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	mInit = true;
	return true;
}

void bloomFBO::Destroy()
{
	for (size_t i = 0; i < mMipChain.size(); ++i)
	{
		if (mMipChain[i].texture != 0)
			glDeleteTextures(1, &mMipChain[i].texture);
	}

	mMipChain.clear();

	if (mFBO != 0)
		glDeleteFramebuffers(1, &mFBO);

	mFBO = 0;
	mInit = false;
}

void bloomFBO::BindForWriting()
{
	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
}

const std::vector<bloomMip>& bloomFBO::MipChain() const
{
	return mMipChain;
}

BloomRenderer::BloomRenderer() :
	mInit(false),
	mSrcViewportSize(0, 0),
	mSrcViewportSizeFloat(0.0f, 0.0f),
	mDownsampleShader(nullptr),
	mUpsampleShader(nullptr)
{
}

BloomRenderer::~BloomRenderer()
{
	Destroy();
}

bool BloomRenderer::Init(unsigned int windowWidth, unsigned int windowHeight)
{
	if (mInit && mSrcViewportSize.x == (int)windowWidth && mSrcViewportSize.y == (int)windowHeight)
		return true;

	Destroy();

	mSrcViewportSize = Vector2<int>((int)windowWidth, (int)windowHeight);
	mSrcViewportSizeFloat = vec2((float)windowWidth, (float)windowHeight);

	const unsigned int num_bloom_mips = 5;
	if (!mFBO.Init(windowWidth, windowHeight, num_bloom_mips))
	{
		std::cerr << "Failed to initialize bloom FBO - cannot create bloom renderer!" << std::endl;
		return false;
	}

	mDownsampleShader = GFX::Shader::Get("downsample");
	mUpsampleShader = GFX::Shader::Get("upsample");
	if (!mDownsampleShader || !mUpsampleShader)
	{
		std::cerr << "Failed to get bloom shaders from atlas." << std::endl;
		Destroy();
		return false;
	}

	mInit = true;
	return true;
}

void BloomRenderer::Destroy()
{
	mFBO.Destroy();
	mDownsampleShader = nullptr;
	mUpsampleShader = nullptr;
	mInit = false;
}

void BloomRenderer::RenderBloomTexture(unsigned int srcTexture, float filterRadius)
{
	if (!mInit || srcTexture == 0)
		return;

	mFBO.BindForWriting();
	RenderDownsamples(srcTexture);
	RenderUpsamples(filterRadius);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, mSrcViewportSize.x, mSrcViewportSize.y);
}

unsigned int BloomRenderer::BloomTexture() const
{
	if (!mInit || mFBO.MipChain().empty())
		return 0;

	return mFBO.MipChain()[0].texture;
}

void BloomRenderer::RenderDownsamples(unsigned int srcTexture)
{
	const std::vector<bloomMip>& mipChain = mFBO.MipChain();
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	if (!mDownsampleShader || !quad || mipChain.empty())
		return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	mDownsampleShader->enable();
	mDownsampleShader->setUniform("srcTexture", 0);
	mDownsampleShader->setUniform("srcResolution", mSrcViewportSizeFloat);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, srcTexture);

	for (size_t i = 0; i < mipChain.size(); ++i)
	{
		const bloomMip& mip = mipChain[i];
		glViewport(0, 0, mip.intSize.x, mip.intSize.y);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.texture, 0);
		quad->render(GL_TRIANGLES);

		mDownsampleShader->setUniform("srcResolution", mip.size);
		glBindTexture(GL_TEXTURE_2D, mip.texture);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	mDownsampleShader->disable();
}

void BloomRenderer::RenderUpsamples(float filterRadius)
{
	const std::vector<bloomMip>& mipChain = mFBO.MipChain();
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	if (!mUpsampleShader || !quad || mipChain.size() < 2)
		return;

	glDisable(GL_DEPTH_TEST);

	mUpsampleShader->enable();
	mUpsampleShader->setUniform("srcTexture", 0);
	mUpsampleShader->setUniform("filterRadius", filterRadius);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glBlendEquation(GL_FUNC_ADD);

	for (int i = (int)mipChain.size() - 1; i > 0; --i)
	{
		const bloomMip& mip = mipChain[i];
		const bloomMip& nextMip = mipChain[i - 1];

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, mip.texture);
		glViewport(0, 0, nextMip.intSize.x, nextMip.intSize.y);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nextMip.texture, 0);
		quad->render(GL_TRIANGLES);
	}

	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
	mUpsampleShader->disable();
}
