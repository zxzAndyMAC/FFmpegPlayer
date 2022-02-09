
#ifndef _FF_SPRITE_H_
#define _FF_SPRITE_H_

#include "cocos2d.h"
#include <IFFmpeg.h>

USING_NS_CC;

class FFSprite : public Sprite
{
public:
	static FFSprite* create();
	FFSprite();
	~FFSprite();

	virtual void draw(Renderer *renderer, const Mat4 &transform, uint32_t flags) override final;

	void play();
	void pause();
	void replay();
	void seek(double time);
private:
	bool start = false;
	bool first = true;
	unsigned int refreshcount = 0;
	size_t readsize;
	char* buffer;
	FFMPEG::FFHandle id_1;
};



#endif

