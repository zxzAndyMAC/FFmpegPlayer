#include "SDLAudio.h"

namespace FFMPEG {

	SDLAudio::SDLAudio() :_audio_dev(0),_pause_on(-1)
	{
	}

	SDLAudio::~SDLAudio()
	{
		av_log(NULL, AV_LOG_INFO, "****SDLAudio release****\n");
		SDL_LockAudioDevice(_audio_dev);
        _AudioMap.clear();
		SDL_UnlockAudioDevice(_audio_dev);
		SDL_CloseAudioDevice(_audio_dev);
	}

	void SDLAudio::sdl_audio_callback(void *opaque, Uint8 *stream, int len)
	{
		SDLAudio* sa = (SDLAudio*)opaque;
		bool mute = true;
		bool clean = true;
		std::unordered_map<unsigned int, MediaState*>::iterator iter;
		for (iter = sa->_AudioMap.begin(); iter != sa->_AudioMap.end(); iter++)
		{
			int ret = iter->second->sdl_audio_callback(stream, len, clean);
            if (ret >= 0) {
                clean = false;
            }
			if (ret > 0)
			{
				mute = false;
			}
		}

		if (mute)
		{
			memset(stream, 0, len);
		}
	}

	void SDLAudio::pause_audiodevice(int pause_on)
	{
        _sdl_mutex.lock();
        if (_pause_on != pause_on) {
            SDL_PauseAudioDevice(_audio_dev, pause_on);
            _pause_on = pause_on;
        }
        _sdl_mutex.unlock();
	}

	int SDLAudio::audio_open(MediaState *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
	{
        _sdl_mutex.lock();
		if (_audio_dev == 0)
		{
            SDL_AudioSpec wanted_spec;
            const char *env;
            static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
    #ifdef ANDROID
            static const int next_sample_rates[] = { 0, 44100, 48000 };
    #else
            static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
    #endif
            int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

            env = SDL_getenv("SDL_AUDIO_CHANNELS");
            if (env) {
                wanted_nb_channels = atoi(env);
                wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
            }
            if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
                wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
                wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
            }
            wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
            wanted_spec.channels = wanted_nb_channels;
            wanted_spec.freq = wanted_sample_rate;
            if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
                av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
                _sdl_mutex.unlock();
                return -1;
            }
            while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
                next_sample_rate_idx--;
            wanted_spec.format = AUDIO_S16SYS;
            wanted_spec.silence = 0;
            int w_per = 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC);
            wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, w_per);
            wanted_spec.callback = SDLAudio::sdl_audio_callback;
            wanted_spec.userdata = this;
            //#ifdef ANDROID
            //        while (SDL_OpenAudio(&wanted_spec, &spec) < 0)
            //#else
#ifdef _WIN32
			while (!(_audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
#else
			while (!(_audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &_spec, SDL_AUDIO_ALLOW_ANY_CHANGE/*SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE*/)))
#endif // _WIN32
                //#endif
            {
                av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
                    wanted_spec.channels, wanted_spec.freq, SDL_GetError());
                wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
                if (!wanted_spec.channels) {
                    wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
                    wanted_spec.channels = wanted_nb_channels;
                    if (!wanted_spec.freq) {
                        av_log(NULL, AV_LOG_ERROR,
                            "No more combinations to try, audio open failed\n");
                        _sdl_mutex.unlock();
                        return -1;
                    }
                }
                wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
            }
            if (_spec.format != AUDIO_S16SYS) {
                av_log(NULL, AV_LOG_ERROR,
                    "SDL advised audio format %d is not supported!\n", _spec.format);
                _sdl_mutex.unlock();
                return -1;
            }
            if (_spec.channels != wanted_spec.channels) {
                wanted_channel_layout = av_get_default_channel_layout(_spec.channels);
                if (!wanted_channel_layout) {
                    av_log(NULL, AV_LOG_ERROR,
                        "SDL advised channel count %d is not supported!\n", _spec.channels);
                    _sdl_mutex.unlock();
                    return -1;
                }
            }
        }

		audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
		audio_hw_params->freq = _spec.freq;
		audio_hw_params->channel_layout = wanted_channel_layout;
		audio_hw_params->channels = _spec.channels;
		audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
		audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
		if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
            _sdl_mutex.unlock();
			return -1;
		}

        _sdl_mutex.unlock();
        
		SDL_LockAudioDevice(_audio_dev);

		_AudioMap.insert(std::pair<unsigned int, MediaState*>(opaque->m_uniqueID, opaque));

		SDL_UnlockAudioDevice(_audio_dev);
		return _spec.size;
	}

	void SDLAudio::close_audio(unsigned int id)
	{
		av_log(NULL, AV_LOG_INFO, "****close_audio****\n");
        SDL_LockAudioDevice(_audio_dev);
		std::unordered_map<unsigned int, MediaState*>::iterator got = _AudioMap.find(id);
		if (got != _AudioMap.end())
		{
			_AudioMap.erase(id);
		}
        SDL_UnlockAudioDevice(_audio_dev);
	}

}
