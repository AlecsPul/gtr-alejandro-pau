#include "bloom.h"

#include <algorithm>
#include <iostream>
#include <vector>

bloomFBO::bloomFBO()
{
	mInit = false;
	mFBO = 0;
}

bloomFBO::~bloomFBO()
{
	Destroy();
}

bool bloomFBO::Init(unsigned int windowWidth, unsigned int windowHeight, unsigned int mipChainLength)
{
	Destroy();

	if (windowWidth == 0 || windowHeight == 0 || mipChainLength == 0)
		return false;

	glGenFramebuffers(1, &mFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

	glm::vec2 mipSize = glm::vec2((float)windowWidth, (float)windowHeight);
	glm::ivec2 mipIntSize = glm::ivec2((int)windowWidth, (int)windowHeight);

	for (unsigned int i = 0; i < mipChainLength; ++i)
	{
		mipSize.x *= 0.5f;
		mipSize.y *= 0.5f;
		mipIntSize.x = (std::max)(1, mipIntSize.x / 2);
		mipIntSize.y = (std::max)(1, mipIntSize.y / 2);

		bloomMip mip;
		mip.size = mipSize;
		mip.intSize = mipIntSize;

		glGenTextures(1, &mip.texture);
		glBindTexture(GL_TEXTURE_2D, mip.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, mip.intSize.x, mip.intSize.y, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.texture, 0);

		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cout << "Error: Bloom framebuffer is not complete: " << status << std::endl;
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			Destroy();
			return false;
		}

		mMipChain.push_back(mip);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	mInit = true;
	return true;
}

void bloomFBO::Destroy()
{
	if (!mMipChain.empty())
	{
		std::vector<GLuint> textures;
		textures.reserve(mMipChain.size());
		for (const bloomMip& mip : mMipChain)
		{
			if (mip.texture != 0)
				textures.push_back(mip.texture);
		}

		if (!textures.empty())
			glDeleteTextures((GLsizei)textures.size(), textures.data());

		mMipChain.clear();
	}

	if (mFBO != 0)
	{
		glDeleteFramebuffers(1, &mFBO);
		mFBO = 0;
	}

	mInit = false;
}

void bloomFBO::BindForWriting()
{
	if (!mInit)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
}

const std::vector<bloomMip>& bloomFBO::MipChain() const
{
	return mMipChain;
}
