#ifndef BLOOM_H
#define BLOOM_H

#include "../core/includes.h"
#include <vector>

#if __has_include(<glm/vec2.hpp>)
#include <glm/vec2.hpp>
#else
namespace glm
{
	struct vec2
	{
		float x;
		float y;

		vec2() : x(0.0f), y(0.0f) {}
		vec2(float x, float y) : x(x), y(y) {}
	};

	struct ivec2
	{
		int x;
		int y;

		ivec2() : x(0), y(0) {}
		ivec2(int x, int y) : x(x), y(y) {}
	};
}
#endif

struct bloomMip
{
	glm::vec2 size;
	glm::ivec2 intSize;
	unsigned int texture = 0;
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

#endif
