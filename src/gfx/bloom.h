#ifndef BLOOM_H
#define BLOOM_H

#include "../core/includes.h"
#include "../core/math.h"
#include <vector>

struct bloomMip
{
	vec2 size;
	Vector2<int> intSize;
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
