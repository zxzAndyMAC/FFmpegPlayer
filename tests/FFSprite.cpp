
#include "FFSprite.h"
#include <stdio.h>
#include <iostream>

using namespace std;
using namespace FFMPEG;

static char* loadbuffer(const char* filename, size_t *readsize)
{
	FILE *fp = fopen(filename, "rb");
	struct stat statBuf;
	auto descriptor = _fileno(fp);

	if (fstat(descriptor, &statBuf) == -1) {
		fclose(fp);
		return 0;
	}
	const size_t size = statBuf.st_size;
	char *buffer = new char[size];
	*readsize = fread(buffer, 1, size, fp);
	fclose(fp);
	return buffer;
}

FFSprite* FFSprite::create()
{
	FFSprite *pVlcSprite = new FFSprite();
	if (pVlcSprite && pVlcSprite->init())
	{
		pVlcSprite->autorelease();
		return pVlcSprite;
	}
	CC_SAFE_DELETE(pVlcSprite);
	return NULL;
}

FFSprite::FFSprite()
{
	IFFMpeg::CreateInstance();
	buffer = loadbuffer("2.flv", &readsize);
	id_1 = IFFMpeg::GetInstance()->create(buffer, readsize, 6);
}

FFSprite::~FFSprite()
{
	IFFMpeg::GetInstance()->release(id_1);
}

void FFSprite::pause()
{
	IFFMpeg::GetInstance()->pause(id_1);
	//_pause = !_pause;
}

void FFSprite::replay()
{
	IFFMpeg::GetInstance()->resume(id_1);
}

void FFSprite::play()
{
	
	IFFMpeg::GetInstance()->play(id_1);
	start = true;
}

void FFSprite::seek(double time)
{
	double _time = IFFMpeg::GetInstance()->getDuration(id_1);
	IFFMpeg::GetInstance()->setSeek(id_1, _time);
}

void FFSprite::draw(Renderer *renderer, const Mat4 &transform, uint32_t flags)
{
	if (start)
	{
		IFFMpeg::GetInstance()->event_loop();
		unsigned int width;
		unsigned int height;
		uint8_t *rgba = IFFMpeg::GetInstance()->getData(id_1, width, height, refreshcount);
		

		if (rgba)
		{
			if (first)
			{
				first = false;
				CCTexture2D *tex = new CCTexture2D;
				CCImage* img = new CCImage;
				img->initWithRawData(rgba, width * height * 4, width, height, 8);
				tex->initWithImage(img);
				initWithTexture(tex);
				tex->autorelease();
				CC_SAFE_DELETE(img);
			}
			else
				_texture->updateWithData(rgba, 0, 0, width, height);
			double time = IFFMpeg::GetInstance()->get_master_clock(id_1);
			printf("time: %0.3f\n",time);
		}
	}
	Sprite::draw(renderer, transform, flags);
}