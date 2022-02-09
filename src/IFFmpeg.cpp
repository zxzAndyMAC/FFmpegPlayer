
#if defined(_FF_DEBUG_)
#include "vld.h"
#endif

#if !defined(_WIN32)
#include "SDL_main.h"
#endif

#include "MediaState.h"
#include "SDLAudio.h"
#include <unordered_map>
#include <queue>
#include <time.h>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

namespace FFMPEG {
	class FFManager : public IFFMpeg
	{
	public:
		FFManager():_id(0)
		{
			_sdlaudio = new SDLAudio;
		}

		~FFManager()
		{
            _media_mutex.lock();
            
			std::unordered_map<FFHandle, MediaState*>::iterator iter = _mediaPlayers.begin();
			for (;iter!=_mediaPlayers.end();iter++)
			{
                //iter->second->mark_exit();
                delete iter->second;
			}
			_mediaPlayers.clear();
			delete _sdlaudio;
			_sdlaudio = nullptr;
            
            _media_mutex.unlock();
		}

		virtual bool pause(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->pause();
			return true;
		} 

		virtual bool setFramedrop(FFHandle id, int fd) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->setFramedrop(fd);
			return true;
		}

		virtual bool resume(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->resume();
			return true;
		}
		
		virtual	bool isPaused( FFHandle id ) override
		{
			MediaState* ms = findMediaPlayer(id);

			if (ms == nullptr)
			{
				return false;
			}
			
			return ms->isPaused();
		}

		virtual bool mute(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->mute();
			return true;
		}

		virtual bool unmute(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->unmute();
			return true;
		}
        
		virtual void reOpenAudioDevice() override
        {

        }

		virtual void mute() override
		{
            _media_mutex.lock();
            
			for ( auto it = _mediaPlayers.begin(); it != _mediaPlayers.end(); ++it )
			{
				it->second->mute();
			}
            
            _media_mutex.unlock();
		}

		virtual void unmute() override
		{
            _media_mutex.lock();
            
			for ( auto it = _mediaPlayers.begin(); it != _mediaPlayers.end(); ++it )
			{
				it->second->unmute();
			}
            
            _media_mutex.unlock();
		}

		virtual FFHandle create( const char *filename, double starttime) override
		{
			FFHandle id = createMediaPlayer(starttime);

			MediaState* ms = findMediaPlayer(id);

			if (ms == nullptr)
			{
				return -1;
			}
            
            if (!ms->create(filename))
            {
                release(id);
                return -1;
            }
            
			return id;
		}

		virtual FFHandle create( char* buffer, unsigned int size, double starttime) override
		{
			FFHandle id = createMediaPlayer(starttime);

			MediaState* ms = findMediaPlayer(id);

			if (ms == nullptr)
			{
				return -1;
			}
            
            if (!ms->create(buffer, size))
            {
                release(id);
                return -1;
            }
            
			return id;
		}

		virtual bool play( FFHandle id, int loopCnt = 1 ) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->play( loopCnt );

			return true;
		}

		virtual unsigned int getWidth(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return -1;
			}
			unsigned int t = ms->getWidth();
			return t;
		}

		virtual unsigned int getHeight(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return -1;
			}
			unsigned int t = ms->getHeight();
			return t;
		}


		virtual double getDuration(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return -1;
			}
			double t = ms->getDuration();
			return t;
		}

		virtual bool setSeek(FFHandle id, double seconds) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
			ms->setSeek(seconds);
			return false;
		}

		virtual double get_master_clock(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return -1;
			}
			double t = ms->get_cur_clock();
			return t;
		}


		virtual void event_loop() override
		{
//			for ( auto it = _mediaPlayers.begin(); it != _mediaPlayers.end(); ++it )
//			{
//				it->second->event_loop();
//			}
		}

		virtual FFMPEG_EVENT getEvent(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return FF_ERROR;
			}

			return ms->getEvent();
		}

		virtual void release() override
		{
            _media_mutex.lock();
            
			std::unordered_map<FFHandle, MediaState*>::iterator iter = _mediaPlayers.begin();

			for (;iter!=_mediaPlayers.end();iter++)
			{
                //iter->second->mark_exit();
                delete iter->second;
			}

			_mediaPlayers.clear();
            
            _media_mutex.unlock();
		}

		virtual bool release(FFHandle id) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return false;
			}
            _media_mutex.lock();
            
			_mediaPlayers.erase(id);
            
            _media_mutex.unlock();
            //ms->mark_exit();
            delete ms;
			return true;
		}

		virtual uint8_t *getData( FFHandle id, unsigned int& width, unsigned int& height, unsigned int& refresh_count ) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return nullptr;
			}


			uint8_t * data = ms->getData(refresh_count);

			if (!data)
			{
				return nullptr;
			}

			width = ms->getWidth();

			height = ms->getHeight();

			return data;
		}

		virtual uint8_t* getThumbnail( FFHandle id, unsigned int& width, unsigned int& height ) override
		{
			MediaState* ms = findMediaPlayer(id);
			if (ms == nullptr)
			{
				return nullptr;
			}

			uint8_t* data = ms->getThumbnail();
			
			width  = ms->getWidth();
			
			height = ms->getHeight();

			return data;
		}

		FFHandle createMediaPlayer(double starttime)
		{
            _media_mutex.lock();
            
			MediaState* ms = new MediaState(++_id, starttime, _sdlaudio);
			_mediaPlayers.insert(std::pair<FFHandle, MediaState*>(_id, ms));
            
            _media_mutex.unlock();
			return _id;
		}
	private:
		MediaState* findMediaPlayer( FFHandle id )
		{
            _media_mutex.lock();
            
			auto it = _mediaPlayers.find( id );

			if ( it == _mediaPlayers.end() )
			{
                _media_mutex.unlock();
                
				return nullptr;
			}
			else
			{
                MediaState* ret = it->second;
                
                _media_mutex.unlock();
                
				return ret;
			}
		}

private:
		std::unordered_map<FFHandle, MediaState*>	_mediaPlayers;
		FFHandle									_id;
		SDLAudio									*_sdlaudio;
        std::mutex                                  _media_mutex;
	};

	///////////////////////////////////////////////////////////////////////////////////

#ifdef ANDROID
#include <android/log.h>
#ifndef LOG_TAG
#define  LOG_TAG    "FFMPEG"
#endif
#define  FFLOGD(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  FFLOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)


static void ffmpeg_log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{
    static int print_prefix = 1;
    static int count;
    static char prev[1024];
    char line[1024];
    static int is_atty;

    av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);

    strcpy(prev, line);
    //sanitize((uint8_t *)line);

    if (level <= AV_LOG_WARNING)
    {
        FFLOGE("%s", line);
    }
    else
    {
        FFLOGD("%s", line);
    }
}
#endif  //ANDROID

	/**********************************************************

	***********************************************************/
	static IFFMpeg* s_ms = nullptr;

	IFFMpeg* IFFMpeg::CreateInstance()
	{
		if (s_ms == nullptr)
		{
			av_log_set_flags(AV_LOG_SKIP_REPEATED);
            avcodec_register_all();
            av_register_all();
            avformat_network_init();
#if defined(ANDROID) && defined(_DEBUG)
            av_log_set_callback(ffmpeg_log_callback_null);
#endif

#if !defined(_WIN32)
            SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
        	SDL_SetMainReady();
        	av_log(NULL, AV_LOG_INFO, "=====SDL_SetMainReady=====\n");
#endif
			int flags =  SDL_INIT_AUDIO | SDL_INIT_TIMER;
			if (!SDL_WasInit(flags))
			{
				if (SDL_Init(flags)) {
					av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
					av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
					return nullptr;
				}	
			}

			s_ms = new FFManager();
		}
		return s_ms;
	}
	
	IFFMpeg* IFFMpeg::GetInstance()
	{
		return s_ms;
	}

	void IFFMpeg::ReleaseInstance()
	{
		if (s_ms != nullptr)
		{
			delete s_ms;
			s_ms = nullptr;
		}

		avformat_network_deinit();

		SDL_Quit();
	}
}
