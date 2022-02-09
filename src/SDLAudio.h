#ifndef SDLAUDIO_MS_H
#define SDLAUDIO_MS_H

#include "MediaState.h"
#include <unordered_map>
#include <mutex>

namespace FFMPEG {

	class MediaState;

	class SDLAudio
	{
	public:
		SDLAudio();
		~SDLAudio();

		int audio_open(MediaState *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);

		void close_audio(unsigned int id);

		void pause_audiodevice(int pause_on);
	
	private:
		//ÒôÆµ»Øµ÷
		static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
	private:
		SDL_AudioSpec		_spec;
		SDL_AudioDeviceID	_audio_dev;
        int                 _pause_on;
        std::mutex          _sdl_mutex;
		std::unordered_map<unsigned int, MediaState*> _AudioMap;
	};

}
#endif
