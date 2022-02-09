#ifndef FFMPEG_MS_H
#define FFMPEG_MS_H

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include "structs.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "IFFmpeg.h"
#include "SDLAudio.h"

namespace FFMPEG {

	struct FFEvent
	{
		int		type;

		double	data;

		FFEvent( int t, double d )
			:type(t)
			,data(d)
		{
		}
	};

	class CPacketQueue;
	class SDLAudio;
	class MediaState
	{
	public:
		MediaState( unsigned int id , double starttime, SDLAudio* sdlaudio);

		~MediaState();
		
		void pause();

		void resume();

		void mute();

		void unmute();
		
		bool isPaused();
        
        bool reOpenAudioDevice();

		unsigned int getWidth();
		unsigned int getHeight();

		bool create(const char *filename);
		bool create(char* buffer, const int64_t size);
		void play( unsigned int loop_times );

		uint8_t *getThumbnail();

		double getDuration();

		double get_cur_clock() {
			_main_mutex.lock();
			double c = _cur_clock;
			_main_mutex.unlock();
			return _cur_clock;
		}

		void event_loop();
		uint8_t *getData(unsigned int& refresh_count);

		FFMPEG_EVENT getEvent() {
            //std::unique_lock<std::mutex> lk(_event_mutex);
			_main_mutex.lock();
            FFMPEG_EVENT ev = _event;
			_main_mutex.unlock();
            return ev;
        }

		void setSeek(double seconds);

		void setFramedrop(int fd) { _framedrop = fd; }

		//��־�ص������������̰߳�ȫ��
		void ff_log_set_callback(void(*callback)(void*, int, const char*, va_list));

		//��Ƶ�ص�
		int sdl_audio_callback(Uint8 *stream, int len, bool clean);
        
        void mark_exit();
        bool is_exit();
	private:
		double get_master_clock();//��ȡ��ʱ��
		double mediaDuration();
		void pushEvent( int type, double data = 0 );
		void do_exit();
		void init_vs();
		void stream_component_close(int stream_index);//�ر���
		void stream_close();//�ر�����
		double get_clock(Clock *c);//��ȡʱ��
		void set_clock(Clock *c, double pts, int serial);
		void set_clock_at(Clock *c, double pts, int serial, double time);
		void set_clock_speed(Clock *c, double speed);
		void init_clock(Clock *c, int *queue_serial);
		void sync_clock_to_slave(Clock *c, Clock *slave);//ͬ������ʱ��
		
		int get_master_sync_type();
		void check_external_clock_speed();//����ⲿʱ���ٶ�, �ⲿʱ��ͬ������Ƶ

		void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes, bool forceSeek = false);//��������
		void stream_toggle_pause();//��ͣ/������Ƶ��

		void update_volume(int sign, double step);//��������
		void step_to_next_frame();//��֡����

		double compute_target_delay(double delay);//������ʱ
		double vp_duration(Frame *vp, Frame *nextvp);//������ʾʱ�� 
		void update_video_pts(double pts, int64_t pos, int serial);//������Ƶ��pts

		int queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);//���Ѿ������֡ѹ���������Ƶ����
		int get_video_frame(AVFrame *frame);//��ȡ��Ƶ֡
		
		void decoder_start(Decoder *d);//��������
		int stream_component_open(int stream_index);//������

		//int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
	
		void toggle_pause();//��ͣ/������Ƶ��
		void toggle_mute();//����
		//////////////////////////////////
		AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
			AVFormatContext *s, AVStream *st, AVCodec *codec);

		int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);

		AVDictionary **setup_find_stream_info_opts(AVFormatContext *s,
			AVDictionary *codec_opts);

		void refresh_loop_wait_event();
		void video_refresh(double *remaining_time);
		void video_display();

		VideoState *stream_open(const char *filename, AVInputFormat *iformat);
		VideoState *stream_open(char *filebuffer, int64_t filebuffersize);

		void video_image_display();

		bool init();

		void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue);//��������ʼ��
		int decoder_decode_frame(Decoder *d, AVFrame *frame);//����������frame֡
		void decoder_destroy(Decoder *d);//���ٽ�����
		void decoder_abort(Decoder *d, FrameQueue *fq);//������ȡ������

		//��Ƶ���
		int audio_decode_frame();
		int synchronize_audio(int nb_samples);//ͬ����Ƶ

		int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
		int is_realtime(AVFormatContext *s);

		bool init_media();
		
		int set_texture_buf(AVFrame *frame, struct SwsContext **img_convert_ctx);

		static int read_buffer(void *opaque, uint8_t *buf, int buf_size);
		static int64_t seek_buffer(void *opaque, int64_t offset, int whence);
		static int decode_interrupt_cb(void *ctx);
		
		void frame_loop();
		//����ѭ��
		void audio_loop();
		void video_loop();
        
        void run();
	public:
		const char		*m_wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };
		AVPacket		m_flush_pkt, m_read_pkt;
		AVDictionary	*m_codec_opts, *m_format_opts;
		int64_t			m_start_time;

		VideoState::ShowMode m_show_mode;

		int				m_infinite_buffer;// ���޻�����
		int				m_loop;//ѭ������
		int64_t			m_duration;

		int				m_startup_volume;
		int				m_av_sync_type;//����Ƶͬ����ʽ
		unsigned int	m_uniqueID;
	private:
		VideoState			*_vs;
		//SDL_AudioDeviceID	_audio_dev;
		int					_framedrop;//����֡
		int64_t				_audio_callback_time;
		double				_remaining_time;

		CPacketQueue		*_packetQueue;
		CFrameQueue			*_frameQueue;
		double				_rdftspeed;
		AVRational			_tb;
		AVRational			_video_frame_rate;

		AVFrame				*_displayFrame;
		AVFrame				*_audio_frame;
		AVFrame				*_video_frame;
		Frame				*_audio_af;
		uint8_t				*_I420;
		uint8_t				*_rgba;
        //uint8_t             *_bgra;

		FFMPEG_EVENT		_event;

		double				_duration;
		double				_cur_clock;

		std::atomic_bool	_audio_loop_end, _video_loop_end;

		std::atomic_bool	_play, _inited, _muted;
		std::atomic_bool	_getFirstPicture;
		std::queue<FFEvent*>_event_queue;
        
        int sample_rate, nb_channels;
        int64_t channel_layout;
		SDLAudio*			_sdlaudio;
        std::atomic_bool    _exit;
        std::atomic_bool    _got_error_frame;
        std::atomic_bool    _buff_change;
        std::atomic_bool    _skip_to_last;
        
        //std::mutex          _exit_mutex;
        //std::mutex          _clock_mutex;
        std::mutex          _main_mutex;
        std::mutex          _event_mutex;
        std::thread*        _runThread;
        
        std::atomic_bool    _picture_size_change;
        unsigned int        _I420_datasize;
        unsigned int        _picture_fresh_count;
	};
}

#endif
