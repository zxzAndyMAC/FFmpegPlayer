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
#include "libyuv.h"

#include <android/log.h>
#include <android/api-level.h>
#include <jni.h>

extern "C"
{
#include <SDL/SDL.h>
}


#define JAVA_CLASSNAME_ACEKID_GAME_ACTIVITY  "com/pengren/acekid/ui/activity/AceKidGameActivity"
//#define JAVA_CLASSNAME_ACEKID_GAME_ACTIVITY  "com/pengren/acekit/activity/AceKidGameActivity"

namespace FFMPEG {

    /**********************************************************

    ***********************************************************/
    class ICameraManager : public ICamera {
    public:
        ~ICameraManager() {
            closeCamera();
        }

        ICameraManager() {
        }

        virtual bool openCamera() override {

            /*
            此处打开摄像头
            */
            JNIEnv *env = Android_JNI_GetEnv();
			jclass java_class = env->FindClass(JAVA_CLASSNAME_ACEKID_GAME_ACTIVITY);
            jmethodID id = env->GetMethodID(java_class, "openCamera", "()V");
            if (NULL == id) return false;
            env->CallVoidMethod(java_instance, id);
            /*
			此处打开摄像头
			*/

            return true;
        }

        virtual void closeCamera() override {

            std::unique_lock <std::mutex> ul(_mutex);
            /*
            此处先关闭摄像头
            */
		   	if(NULL != java_instance)
			{
				JNIEnv *env = Android_JNI_GetEnv();
				jclass java_class = env->FindClass(JAVA_CLASSNAME_ACEKID_GAME_ACTIVITY);
				jmethodID id = env->GetMethodID(java_class, "closeCamera", "()V");
				if (NULL != id)
					env->CallVoidMethod(java_instance, id);
				env->DeleteGlobalRef(java_instance);
				java_instance = NULL;
			}
            /*
			此处先关闭摄像头
			*/

            if (NULL != _rgba) {
                free(_rgba);
                _rgba = NULL;
            }
            if (NULL != _I420) {
                free(_I420);
                _I420 = NULL;
            }

            if (NULL != _I420_origin) {
                free(_I420_origin);
                _I420_origin = NULL;
            }
			if (NULL != _copy_buffer) {
                free(_copy_buffer);
                _copy_buffer = NULL;
            }
        }

        virtual uint8_t *getData(unsigned int &width, unsigned int &height, unsigned int& refresh_count) override {
            //std::unique_lock <std::mutex> ul(_mutex);
            if (refresh_count >= _refresh_count) {
                return NULL;
            }

            refresh_count = _refresh_count;
            width = _width;
            height = _height;
		
            if(_buffer_change) {
                _mutex.lock();

                if(NULL != _copy_buffer && _res_change)
                {
                    free(_copy_buffer);
                    _copy_buffer = (uint8_t *)malloc(((_width * _height * 3) >> 1) * sizeof(uint8_t));
                }
                if(NULL == _copy_buffer)
                    _copy_buffer = (uint8_t *)malloc(((_width * _height * 3) >> 1) * sizeof(uint8_t));
                memset(_copy_buffer, 0, ((_width * _height * 3) >> 1) * sizeof(uint8_t));
                memcpy(_copy_buffer, _I420_origin, ((_width * _height * 3) >> 1) * sizeof(uint8_t));
                _buffer_change = false;

                _mutex.unlock();
            }else{
                return NULL;
            }

            if (_res_change && NULL != _rgba) {
                _res_change = false;
                free(_rgba);
                _rgba = (uint8_t *) malloc(height * width * 4 * sizeof(uint8_t));

                free(_I420);
                _I420 = (uint8_t *) malloc((width * height * 3) >> 1 * sizeof(uint8_t));
            }

            if (_rgba == NULL) {
                _rgba = (uint8_t *) malloc(height * width * 4 * sizeof(uint8_t));
                _I420 = (uint8_t *) malloc((width * height * 3) >> 1 * sizeof(uint8_t));
            }

            memset(_rgba, 0, height * width * 4 * sizeof(uint8_t));
            memset(_I420, 0, (width * height * 3) >> 1 * sizeof(uint8_t));

            libyuv::I420Mirror(
                    &_copy_buffer[0], width,
                    &_copy_buffer[width * height], width >> 1,
                    &_copy_buffer[(width * height * 5) >> 2], width >> 1,
                    &_I420[0], width,
                    &_I420[width * height], width >> 1,
                    &_I420[(width * height * 5) >> 2], width >> 1,
                    width, height
            );

            libyuv::I420ToABGR(
                            &_I420[0], width,
                            &_I420[width * height], width >> 1,
                            &_I420[(width * height * 5) >> 2], width >> 1,
                            _rgba, width * 4,
                            width, height
                           );

            return _rgba;
        }

    public:
        void setData(JNIEnv *env, unsigned int width, unsigned int height, jbyteArray array) {
			if(NULL == java_instance) return;
            int len = env->GetArrayLength(array);
            //unsigned char* buffer = (unsigned char*)malloc(sizeof(unsigned char) * len);
            //memset(buffer,0,len);
            //env->GetByteArrayRegion(array, 0, len, (jbyte*)buffer);
            _mutex.lock();
            if (_I420_origin == NULL) {
                _width = width;
                _height = height;
                _I420_origin = (uint8_t *) malloc((width * height * 3) >> 1 * sizeof(uint8_t));
            }

            if (_width != width || _height != height) {
				_res_change = true;
                if (_I420_origin != NULL) {
                    free(_I420_origin);
                    _I420_origin = (uint8_t *) malloc(height * width * 4 * sizeof(uint8_t));
                }
            }

            memset(_I420_origin, 0, (width * height * 3) >> 1 * sizeof(uint8_t));
            

            env->GetByteArrayRegion(array, 0, len, (jbyte *) _I420_origin);


			_buffer_change = true;
            ++_refresh_count;
            _mutex.unlock();
        }

    private:
        unsigned int _refresh_count = 0;
        unsigned int _width = 0;
        unsigned int _height = 0;
		bool 		_res_change = false;
    	bool 		_buffer_change = false;
        uint8_t *_rgba = NULL;
        uint8_t *_I420 = NULL;
        uint8_t *_I420_origin = NULL;
		uint8_t *_copy_buffer = NULL;
        std::mutex _mutex;

    public:
        jobject java_instance = NULL;
    };

    /**********************************************************

    ***********************************************************/
    static ICamera *S_ICamera = nullptr;
    static std::mutex S_requestMutex;

    ICamera *ICamera::CreateInstance() {
		std::unique_lock <std::mutex> ul(S_requestMutex);
        if (S_ICamera == nullptr) {
            S_ICamera = new ICameraManager();
        }

        return S_ICamera;
    }

    ICamera *ICamera::GetInstance() {
        std::unique_lock <std::mutex> ul(S_requestMutex);
        if (nullptr == S_ICamera)
		{
			ul.unlock();
            ICamera::CreateInstance();
		}
        return S_ICamera;
    }

    void ICamera::ReleaseInstance() {
        if (S_ICamera != nullptr) {
            delete S_ICamera;

            S_ICamera = nullptr;
        }
    }
}

extern "C"
{

JNIEXPORT void JNICALL
Java_com_pengren_acekid_ui_activity_AceKidGameActivity_setData(JNIEnv * env , jobject thiz, jint w , jint h, jbyteArray jbuffer )
{
	(( FFMPEG::ICameraManager* ) FFMPEG::ICamera::GetInstance()) -> setData(env, w, h, jbuffer);
}

JNIEXPORT void JNICALL
Java_com_pengren_acekid_ui_activity_AceKidGameActivity_setCameraContext(JNIEnv *env, jobject thiz, jobject context) 
{
	((FFMPEG::ICameraManager*)FFMPEG::ICamera::GetInstance())->java_instance = env->NewGlobalRef(context);
}

}
