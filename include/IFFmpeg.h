#pragma once

#if defined(_WIN32)
#	if defined(IFF_EXPORTS)
#		define IFF_DLL __declspec(dllexport)
#	else
#		define IFF_DLL __declspec(dllimport)
#	endif
#elif defined(__APPLE__)
#	define IFF_DLL __attribute__((visibility("default")))
#else
#	define IFF_DLL
#endif

namespace FFMPEG {

	typedef unsigned int FFHandle;

	typedef enum FFMPEG_EVENT
	{
		FF_ERROR = 100,//���󣬱��粻���ڵ�FFHandle
		FF_HOLD,//��δ����play
		FF_NONE,//���¼�
		FF_PAUSE,//��ͣ
		FF_MUTE,//����
		FF_OVER,//���Ž���
		FF_QUIT,//�˳�
		FF_PLAY
	}FFMPEG_EVENT;
	
	class IFF_DLL ICamera
	{
	public:
		static ICamera*			CreateInstance();

		static void				ReleaseInstance();

		static ICamera*			GetInstance();

	public:
		virtual ~ICamera(){}

		virtual bool			openCamera() = 0; //��������ͷ

		virtual void			closeCamera() = 0; //�ر�����ͷ
        
		virtual uint8_t*		getData( unsigned int& width, unsigned int& height, unsigned int& refresh_count) = 0;//��ȡ����ͷ��Ƶͼ��ÿ֡���ݣ������Ի棬��������ڷ���null
	};

	class IFF_DLL IFFMpeg
	{
	public:
		static IFFMpeg*			CreateInstance();

		static void				ReleaseInstance();

		static IFFMpeg*			GetInstance();

	public:
		virtual ~IFFMpeg(){}
		
		virtual FFHandle		create( const char *filename, double starttime) = 0; //ͨ���ļ�����,��������ڵ�id����false

		virtual FFHandle		create( char* buffer, unsigned int size, double starttime) = 0; //ͨ���ڴ沥��,��������ڵ�id����false
        
        virtual void            reOpenAudioDevice() = 0;

		/*����Ƶ��ͬ������
		����ԭ����CPU�ڴ�����Ƶ֡��ʱ�����̫����Ĭ�ϵ�����Ƶͬ����������Ƶͬ������Ƶ, ��������Ƶ���Ź��죬��Ƶ�����ϡ�
		framedrop ����������֡�ķ�Χ������ͨ���޸� framedrop ����ֵ�������ͬ�������⣬framedrop ������Ƶ֡����������ʱ����һЩ֡�ﵽͬ����Ч����
		*/
		virtual bool			setFramedrop(FFHandle id, int fd) = 0;
		
		virtual void			release() = 0; //�ͷ�ȫ��

		virtual bool			release(FFHandle id) = 0; //�ͷ�player

		virtual bool			pause(FFHandle id) = 0; //��ͣ,��������ڵ�id����false
		
		virtual bool			resume(FFHandle id) = 0; //�ָ�,��������ڵ�id����false
		
		virtual	bool			isPaused( FFHandle id ) = 0;

		virtual bool			mute(FFHandle id) = 0; //����,��������ڵ�id����false

		virtual bool			unmute(FFHandle id) = 0;//ȡ������,��������ڵ�id����false
		
		virtual void			mute() = 0; //����,��������ڵ�id����false

		virtual void			unmute() = 0;//ȡ������,��������ڵ�id����false

		virtual bool			play( FFHandle id, int loopCnt = 1 ) = 0;//������Ƶ

		virtual bool			setSeek(FFHandle id, double seconds) = 0;//������ˣ���Ϊ��λ�����Ϊ����������Ϊ������secondsΪ��Ҫ�������˵�����

		virtual unsigned int	getWidth(FFHandle id) = 0;//��������ڵ�id����-1

		virtual unsigned int	getHeight(FFHandle id) = 0;//��������ڵ�id����-1

		virtual double			getDuration(FFHandle id) = 0; //��ȡ��Ƶʱ��,��������ڵ�id����-1

		virtual double			get_master_clock(FFHandle id) = 0;//��ȡ��ǰ���Ž���,��������ڵ�id����-1

		virtual void			event_loop() = 0; //�����¼�ѭ����ÿ֡����
			
		virtual FFMPEG_EVENT	getEvent( FFHandle id ) = 0;
		
		virtual uint8_t*		getData( FFHandle id, unsigned int& width, unsigned int& height, unsigned int& refresh_count) = 0;//��ȡ��Ƶͼ��ÿ֡���ݣ������Ի棬��������ڷ���null

		virtual uint8_t*		getThumbnail( FFHandle id, unsigned int& width, unsigned int& height ) = 0; //��ȡ����ͼ
	};
}
