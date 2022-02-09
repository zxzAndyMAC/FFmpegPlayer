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

#import "ACECamera.h"

namespace FFMPEG {
	
	/**********************************************************

	***********************************************************/
	class ICameraManager : public ICamera
	{
	public:
		~ICameraManager()
		{
            [_ace_camera closeCamera];
		}
        
        ICameraManager()
        {
            _ace_camera = [[ACECamera alloc] init];
        }
		
		virtual bool openCamera() override
		{
			return [_ace_camera openCamera];
		}

		virtual void closeCamera() override
		{
            [_ace_camera closeCamera];
		}
        
		virtual uint8_t* getData( unsigned int& width, unsigned int& height, unsigned int& refresh_count ) override
		{
            width = [_ace_camera getWidth];
            height = [_ace_camera getHeight];
			return [_ace_camera getBuffer:refresh_count];
		}
        
    private:
        id _ace_camera;
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
        if (nullptr == S_ICamera)
            ICamera::CreateInstance();
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
