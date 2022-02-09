
#ifndef ANDROID

#include <thread>
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

#include "IFFmpeg.h"

namespace FFMPEG {
	
	/**********************************************************

	***********************************************************/


	class ICameraManager : public ICamera
	{
	public:
		~ICameraManager()
		{

		}
		
		virtual bool openCamera() override
		{
			return false;
		}

		virtual void closeCamera() override
		{
		
		}
        
		virtual uint8_t* getData( unsigned int& width, unsigned int& height, unsigned int& refresh_count ) override
		{
			return NULL;
		}
	};

	/**********************************************************

	***********************************************************/
	static ICamera* S_ICamera = nullptr;
	
	ICamera* ICamera::CreateInstance()
	{
		if ( S_ICamera == nullptr )
		{
			S_ICamera = new ICameraManager();
		}

		return S_ICamera;
	}
	
	ICamera* ICamera::GetInstance()
	{
		return S_ICamera;
	}

	void ICamera::ReleaseInstance()
	{
		if ( S_ICamera != nullptr )
		{
			delete S_ICamera;

			S_ICamera = nullptr;
		}
	}


}

#endif
