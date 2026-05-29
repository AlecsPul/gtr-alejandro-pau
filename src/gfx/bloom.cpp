#include "bloom.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <vector>

bloomFBO::bloomFBO() : mInit(false) {}
bloomFBO::~bloomFBO() {}


bool bloomFBO::Init(unsigned int windowWidth, unsigned int windowHeight, unsigned int mipChainLength)
{
    if (mInit) return true;

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

    vec2 mipSize((float)windowWidth, (float)windowHeight);
    Vector2<int> mipIntSize((int)windowWidth, (int)windowHeight);
    // Safety check
    if (windowWidth > (unsigned int)INT_MAX || windowHeight > (unsigned int)INT_MAX) {
        std::cerr << "Window size conversion overflow - cannot build bloom FBO!\n";
        return false;
    }

    for (unsigned int i = 0; i < mipChainLength; i++)
    {
        bloomMip mip;

        mipSize *= 0.5f;
        mipIntSize.x /= 2;
        mipIntSize.y /= 2;
        mip.size = mipSize;
        mip.intSize = mipIntSize;

        glGenTextures(1, &mip.texture);
        glBindTexture(GL_TEXTURE_2D, mip.texture);
        // we are downscaling an HDR color buffer, so we need a float texture format
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F,
            (int)mipSize.x, (int)mipSize.y,
            0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        mMipChain.emplace_back(mip);
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, mMipChain[0].texture, 0);

    // setup attachments
    unsigned int attachments[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, attachments);

    // check completion status
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("Bloom FBO error, status: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mInit = true;
    return true;
}

void bloomFBO::Destroy()
{
    for (int i = 0; i < mMipChain.size(); i++) {
        glDeleteTextures(1, &mMipChain[i].texture);
        mMipChain[i].texture = 0;
    }
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

bool BloomRenderer::Init(unsigned int windowWidth, unsigned int windowHeight)
{
    if (mInit) return true;
    mSrcViewportSize = Vector2<int>(windowWidth, windowHeight);
    mSrcViewportSizeFloat = vec2((float)windowWidth, (float)windowHeight);

    // Framebuffer
    const unsigned int num_bloom_mips = 5; // Experiment with this value
    bool status = mFBO.Init(windowWidth, windowHeight, num_bloom_mips);
    if (!status) {
        std::cerr << "Failed to initialize bloom FBO - cannot create bloom renderer!\n";
        return false;
    }

    // Shaders
    mDownsampleShader = GFX::Shader::Get("downsample");
    mUpsampleShader = GFX::Shader::Get("upsample");

    // Downsample
    mDownsampleShader->enable();
    mDownsampleShader->setUniform("srcTexture", texture, 0);
    mDownsampleShader->disable();

    // Upsample
    mUpsampleShader->enable();
    mUpsampleShader->setUniform("srcTexture", texture, 0);
    mUpsampleShader->disable();

    mInit = true;
    return true;
}