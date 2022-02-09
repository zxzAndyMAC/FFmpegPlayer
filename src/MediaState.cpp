
#if defined(_FF_DEBUG_)
#include "vld.h"
#endif
#include "MediaState.h"
#include "FFMpegUtils.h"
#include <libyuv.h>

namespace FFMPEG {

	int MediaState::read_buffer(void *opaque, uint8_t *buf, int buf_size)
	{
		MediaState *ms = (MediaState*)opaque;
		buf_size = FFMIN(buf_size, ms->_vs->filebuffersize);
		if (!buf_size)
			return AVERROR_EOF;

		memcpy(buf, ms->_vs->filebuffer, buf_size);
		ms->_vs->filebuffer += buf_size;
		ms->_vs->filebuffersize -= buf_size;
		ms->_vs->filepos += buf_size;

		return buf_size;
	}

	int64_t MediaState::seek_buffer(void *opaque, int64_t offset, int whence)
	{
		MediaState *ms = (MediaState*)opaque;

		if (whence == AVSEEK_SIZE)
			return ms->_vs->filesize;
		else if (whence == SEEK_CUR)
			whence = ms->_vs->filepos;
		else if (whence == SEEK_END)
			whence = ms->_vs->filesize;

		int64_t moveset = whence + offset;
		ms->_vs->filebuffer -= ms->_vs->filepos;
		ms->_vs->filebuffer += moveset;
		ms->_vs->filepos = moveset;
		ms->_vs->filebuffersize = ms->_vs->filesize - ms->_vs->filepos;

		return ms->_vs->filepos;
	}

	MediaState::MediaState(unsigned int id, double starttime, SDLAudio* sdlaudio) :
		m_codec_opts(NULL),
		m_format_opts(NULL),
		//m_start_time((int64_t)(starttime * AV_TIME_BASE)),
        m_start_time(starttime>DBL_EPSILON ? (int64_t)(starttime * AV_TIME_BASE) : AV_NOPTS_VALUE),
		m_infinite_buffer(-1),
		m_duration(AV_NOPTS_VALUE),
		m_loop(1),
		m_startup_volume(100),		
		m_av_sync_type(AV_SYNC_AUDIO_MASTER),
		m_show_mode(VideoState::ShowMode::SHOW_MODE_NONE),
		_rdftspeed(0.02),
		_remaining_time(0.0),
		_packetQueue(NULL),
		_frameQueue(NULL),
		_framedrop(-1),
		_displayFrame(NULL),
		_rgba(NULL),
        _I420(NULL),
		_audio_callback_time(0),
        _I420_datasize(0),
		_vs(NULL),
		_event(FF_HOLD),
		_play(false),
		_audio_frame(NULL),
        _buff_change(false),
		_video_frame(NULL),
        _exit(false),
        _runThread(nullptr),
        _got_error_frame(false),
        _skip_to_last(false),
        _picture_fresh_count(0),
        _picture_size_change(false),
        _inited(false),
        _muted(false),
		_cur_clock(0),
		_getFirstPicture(false)
	{
		m_uniqueID = id;
		_sdlaudio = sdlaudio;
		_packetQueue = new CPacketQueue(this);
		_frameQueue = new CFrameQueue();
	}

	MediaState::~MediaState()
	{
		av_log(NULL, AV_LOG_INFO, "****mark_exit id:%d****\n", this->m_uniqueID);
        mark_exit();
		av_log(NULL, AV_LOG_INFO, "****do_exit id:%d****\n", this->m_uniqueID);
		do_exit();

		if (_packetQueue != NULL)
		{
			delete _packetQueue;
		}

		if (_frameQueue != NULL)
		{
			delete _frameQueue;
		}

		while (!_event_queue.empty())
		{
			FFEvent* ev = _event_queue.front();
			_event_queue.pop();
			delete ev;
		}
        
        if (_runThread) {
            delete _runThread;
        }
	}

    void MediaState::mark_exit()
    {
        //std::unique_lock<std::mutex> lk(_exit_mutex);
        _exit = true;
        //lk.unlock();
        
        if (_runThread) {
            _runThread->join();
        }
    }

    bool MediaState::is_exit()
    {
        //std::unique_lock<std::mutex> lk(_exit_mutex);
        return _exit;
    }

	void MediaState::ff_log_set_callback(void(*callback)(void*, int, const char*, va_list))
	{
		av_log_set_callback(callback);
	}

	void MediaState::play(unsigned int loop_times)
	{
        _event_mutex.lock();
		_event = FF_NONE;
		_play = true;
		m_loop = loop_times;
		_getFirstPicture = false;
        _event_mutex.unlock();
		pushEvent( FF_PLAY_EVENT );
	}

	void MediaState::pushEvent( int type, double data )
	{
        _event_mutex.lock();
		FFEvent* event = new FFEvent(type, data);
		_event_queue.push(event);
        _event_mutex.unlock();
	}

    void MediaState::run()
    {
        unsigned int sleep_time = 1;
        #ifdef __APPLE__
        sleep_time = 2;
        #elif defined(ANDROID)
        sleep_time = 1;
        #endif
        for (;;) {
            //std::unique_lock<std::mutex> lk(_exit_mutex);
            if (_exit) {
                break;
            }
            //lk.unlock();
            _main_mutex.lock();
            event_loop();
			_cur_clock = get_master_clock();
            _main_mutex.unlock();
            #ifdef _WIN32
            Sleep(sleep_time);
            #else
            if (sleep_time >0) {
                struct timeval tval;

                tval.tv_sec= sleep_time / 1000;

                tval.tv_usec= (sleep_time * 1000) % 1000000;

                select(0,NULL,NULL,NULL,&tval);
            }else{
                sleep(0);
            }
            #endif
        }
    }

	bool MediaState::create(const char *filename)
	{
		if (!init())
		{
			av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
			return false;
		}
        
		if (!stream_open(filename, NULL)) {
			av_log(NULL, AV_LOG_FATAL, "Failed to open stream!\n");
			return false;
		}
        
        _inited = true;
        
        _runThread = new (std::nothrow) std::thread(&MediaState::run, this);
        
		return true;
	}

	bool MediaState::create(char* buffer, const int64_t size)
	{
		if (!init())
		{
			av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
			return false;
		}

		if (!stream_open(buffer, size)) {
			av_log(NULL, AV_LOG_FATAL, "Failed to open stream!\n");
			return false;
		}
        
        _inited = true;
        
        _runThread = new (std::nothrow) std::thread(&MediaState::run, this);
        
		return true;
	}

	bool MediaState::init()
	{
		_play = false;
		_event = FF_HOLD;
		_audio_loop_end = false;
		_video_loop_end = false;

		av_init_packet(&m_flush_pkt);
		m_flush_pkt.data = (uint8_t *)&m_flush_pkt;

		return true;
	}

	void MediaState::decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue)
	{
        av_log(NULL, AV_LOG_INFO, "===== decoder_init =====\n");
		memset(d, 0, sizeof(Decoder));
		d->avctx = avctx;
		d->queue = queue;
		d->start_pts = AV_NOPTS_VALUE;
		d->pkt_serial = -1;
	}

	int MediaState::decoder_decode_frame(Decoder *d, AVFrame *frame)
	{
		int ret = AVERROR(EAGAIN);
		AVPacket pkt;
		
		if (d->queue->serial == d->pkt_serial) {
			do {
				if (d->queue->abort_request)
					return -1;

				switch (d->avctx->codec_type) {
				case AVMEDIA_TYPE_VIDEO:
					ret = avcodec_receive_frame(d->avctx, frame);
					if (ret >= 0) {
						frame->pts = frame->best_effort_timestamp;
					}
					break;
				case AVMEDIA_TYPE_AUDIO:
					ret = avcodec_receive_frame(d->avctx, frame);
					if (ret >= 0) {
						AVRational tb{ 1, frame->sample_rate };
						if (frame->pts != AV_NOPTS_VALUE)
							frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
						else if (d->next_pts != AV_NOPTS_VALUE)
							frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
						if (frame->pts != AV_NOPTS_VALUE) {
							d->next_pts = frame->pts + frame->nb_samples;
							d->next_pts_tb = tb;
						}
					}
					break;
				default:
					break;
				}
				if (ret == AVERROR_EOF) {
					d->finished = d->pkt_serial;
					avcodec_flush_buffers(d->avctx);
					return 0;
				}
				if (ret >= 0)
					return 1;
			} while (ret != AVERROR(EAGAIN));
		}

		int err = _packetQueue->packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial);
		if (err == -2)//no packet yet
			return -2;
		else if (err < 0)
			return -1;

		if (pkt.data == m_flush_pkt.data) {
			//ˢ�±�ǲ�ʹ�ô�buffer
			avcodec_flush_buffers(d->avctx);
			d->finished = 0;
			d->next_pts = d->start_pts;
			d->next_pts_tb = d->start_pts_tb;
			return -2;
		}
		else {
			if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
				//do nothing
			}
			else {
				if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
					av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
					av_packet_move_ref(&d->pkt, &pkt);
					av_packet_move_ref(&pkt, &d->pkt);
					return AVERROR(EAGAIN);
				}
			}
			av_packet_unref(&pkt);
			return -2;
		}
	}

	void MediaState::decoder_destroy(Decoder *d)
	{
        av_log(NULL, AV_LOG_INFO, "===== decoder_destroy =====\n");
		av_packet_unref(&d->pkt);
		avcodec_free_context(&d->avctx);
	}

	void MediaState::decoder_abort(Decoder *d, FrameQueue *fq)
	{
        av_log(NULL, AV_LOG_INFO, "===== decoder_abort =====\n");
		_packetQueue->packet_queue_abort(d->queue);
		_frameQueue->frame_queue_signal(fq);
		_packetQueue->packet_queue_flush(d->queue);
	}

	void MediaState::stream_component_close(int stream_index)
	{
		AVFormatContext *ic = _vs->ic;
        enum AVMediaType stream_type = ic->streams[stream_index]->codecpar ? ic->streams[stream_index]->codecpar->codec_type : ic->streams[stream_index]->codec->codec_type;

		if (stream_index < 0 || stream_index >= ic->nb_streams)
			return;

		switch (stream_type) {
		case AVMEDIA_TYPE_AUDIO:
            av_log(NULL, AV_LOG_INFO, "===== decoder_abort audio =====\n");
			decoder_abort(&_vs->auddec, &_vs->sampq);
			audio_loop();
			//SDL_CloseAudioDevice(_audio_dev);
			_sdlaudio->close_audio(m_uniqueID);
            av_log(NULL, AV_LOG_INFO, "===== decoder_destroy audio =====\n");
			decoder_destroy(&_vs->auddec);
			if(_vs->swr_ctx)
				swr_free(&_vs->swr_ctx);
			if (_vs->audio_buf1)
			{
				av_freep(&_vs->audio_buf1);
				_vs->audio_buf1_size = 0;
				_vs->audio_buf = NULL;
			}
			break;
		case AVMEDIA_TYPE_VIDEO:
            av_log(NULL, AV_LOG_INFO, "===== decoder_abort video =====\n");
			decoder_abort(&_vs->viddec, &_vs->pictq);
			video_loop();
            av_log(NULL, AV_LOG_INFO, "===== decoder_destroy video =====\n");
			decoder_destroy(&_vs->viddec);
			break;
		default:
			break;
		}

		ic->streams[stream_index]->discard = AVDISCARD_ALL;
		switch (stream_type) {
		case AVMEDIA_TYPE_AUDIO:
			_vs->audio_st = NULL;
			_vs->audio_stream = -1;
			break;
		case AVMEDIA_TYPE_VIDEO:
			_vs->video_st = NULL;
			_vs->video_stream = -1;
			break;
		default:
			break;
		}
	}

	void MediaState::stream_close()
	{
		av_log(NULL, AV_LOG_INFO, "===== stream_close =====\n");
		_vs->abort_request = 1;

		if (!_play)
		{
			if (_audio_frame)
			{
				av_frame_free(&_audio_frame);
			}
			if (_video_frame)
			{
				av_frame_free(&_video_frame);
			}
		}

		if (_vs->ic)
		{
			/* close each stream */
			if (_vs->audio_stream >= 0)
				stream_component_close(_vs->audio_stream);
			if (_vs->video_stream >= 0)
				stream_component_close(_vs->video_stream);

			avformat_close_input(&_vs->ic);
		}
		
		if (_play) 
		{
			_packetQueue->packet_queue_destroy(&_vs->videoq);
			_packetQueue->packet_queue_destroy(&_vs->audioq);

			/* free all pictures */
			_frameQueue->frame_queue_destory(&_vs->pictq);
			_frameQueue->frame_queue_destory(&_vs->sampq);
		}

		_packetQueue->packet_mutex_destroy(&_vs->videoq);
		_packetQueue->packet_mutex_destroy(&_vs->audioq);

		_frameQueue->frame_mutex_destory(&_vs->pictq);
		_frameQueue->frame_mutex_destory(&_vs->sampq);

		if(_vs->img_convert_ctx)
			sws_freeContext(_vs->img_convert_ctx);
		if (_vs->filename)
			av_free(_vs->filename);

		if (_displayFrame)
		{
			av_free(_displayFrame->data[0]);
			av_frame_free(&_displayFrame);
            _displayFrame = NULL;
		}
        
		if (_rgba)
		{
			av_free(_rgba);
            av_free(_I420);
            _I420 = NULL;
			_rgba = NULL;
		}

		av_free(_vs);
		_vs = NULL;
		_play = false;
		av_log(NULL, AV_LOG_INFO, "===== stream_close_done =====\n");
	}

	void MediaState::do_exit()
	{
		if (_vs) {
			stream_close();
			av_log(NULL, AV_LOG_QUIET, "%s", "");
		}
	}

	double MediaState::get_clock(Clock *c)
	{
		if (_event == FF_OVER)
		{
			return mediaDuration();
		}
		if (*c->queue_serial != c->serial)
			return 0;
		if (c->paused || 1 == _vs->abort_request || _got_error_frame) {
			return c->pts;
		}
		else {
			double time = av_gettime_relative() / 1000000.0;
			return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
		}
	}

	void MediaState::set_clock_at(Clock *c, double pts, int serial, double time)
	{
		c->pts = pts;
		c->last_updated = time;
		c->pts_drift = c->pts - time;
		c->serial = serial;
	}

	void MediaState::set_clock(Clock *c, double pts, int serial)
	{
		double time = av_gettime_relative() / 1000000.0;
		set_clock_at(c, pts, serial, time);
	}

	void MediaState::set_clock_speed(Clock *c, double speed)
	{
		set_clock(c, get_clock(c), c->serial);
		c->speed = speed;
	}

	void MediaState::init_clock(Clock *c, int *queue_serial)
	{
		c->speed = 1.0;
		c->paused = 0;
		c->queue_serial = queue_serial;
		set_clock(c, NAN, -1);
	}

	void MediaState::sync_clock_to_slave(Clock *c, Clock *slave)
	{
		double clock = get_clock(c);
		double slave_clock = get_clock(slave);
		if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
			set_clock(c, slave_clock, slave->serial);
	}

	int MediaState::get_master_sync_type() {
		if (_vs->av_sync_type == AV_SYNC_VIDEO_MASTER) {
			if (_vs->video_st)
				return AV_SYNC_VIDEO_MASTER;
			else
				return AV_SYNC_AUDIO_MASTER;
		}
		else if (_vs->av_sync_type == AV_SYNC_AUDIO_MASTER) {
			if (_vs->audio_st)
				return AV_SYNC_AUDIO_MASTER;
			else
				return AV_SYNC_EXTERNAL_CLOCK;
		}
		else {
			return AV_SYNC_EXTERNAL_CLOCK;
		}
	}

	double MediaState::get_master_clock()
	{
		double val = 0;
		if (_vs -> paused)
		{
            val = get_clock(&_vs->vidclk);
            return val;
		}
		
		switch (get_master_sync_type()) {
		case AV_SYNC_VIDEO_MASTER:
			val = get_clock(&_vs->vidclk);
			break;
		case AV_SYNC_AUDIO_MASTER:
			val = get_clock(&_vs->audclk);
			break;
		default:
			val = get_clock(&_vs->extclk);
			break;
		}
		return val;
	}

	void MediaState::check_external_clock_speed() 
	{
		if ((_vs->video_stream >= 0 && _vs->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
			(_vs->audio_stream >= 0 && _vs->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)) {
			set_clock_speed(&_vs->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, _vs->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
		}
		else if ((_vs->video_stream < 0 || _vs->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
			(_vs->audio_stream < 0 || _vs->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
			set_clock_speed(&_vs->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, _vs->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
		}
		else {
			double speed = _vs->extclk.speed;
			if (speed != 1.0)
				set_clock_speed(&_vs->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
		}
	}

	void MediaState::setSeek( double seconds )
	{
		pushEvent( FF_SEEK_EVENT, seconds );
	}

	void MediaState::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes, bool forceSeek)
	{
		if (!_vs->seek_req || forceSeek) {
			_vs->seek_pos = pos;
			_vs->seek_rel = rel;
			_vs->seek_flags &= ~AVSEEK_FLAG_BYTE;
			if (seek_by_bytes)
				_vs->seek_flags |= AVSEEK_FLAG_BYTE;
			_vs->seek_req = 1;
		}
	}

	void MediaState::stream_toggle_pause()
	{
		if (_vs->paused) {
			_vs->frame_timer += av_gettime_relative() / 1000000.0 - _vs->vidclk.last_updated;
			if (_vs->read_pause_return != AVERROR(ENOSYS)) {
				_vs->vidclk.paused = 0;
			}
			set_clock(&_vs->vidclk, get_clock(&_vs->vidclk), _vs->vidclk.serial);
		}
		set_clock(&_vs->extclk, get_clock(&_vs->extclk), _vs->extclk.serial);
		_vs->paused = _vs->audclk.paused = _vs->vidclk.paused = _vs->extclk.paused = !_vs->paused;
	}

	void MediaState::toggle_pause()
	{
		stream_toggle_pause();
		_vs->step = 0;
	}

	void MediaState::toggle_mute()
	{
		_muted = !_muted;
	}

	void MediaState::update_volume(int sign, double step)
	{
		double volume_level = _vs->audio_volume ? (20 * log(_vs->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
		int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
		_vs->audio_volume = av_clip(_vs->audio_volume == new_volume ? (_vs->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
	}

	void MediaState::step_to_next_frame()
	{
		av_log(NULL, AV_LOG_WARNING,
			"Media has been paused, step_to_next_frame\n");
		/* if the stream _vs paused unpause it, then step */
		if (_vs->paused)
			stream_toggle_pause();
		_vs->step = 1;
	}

	double MediaState::compute_target_delay(double delay)
	{
		double sync_threshold, diff = 0;

		// �����������Ƶ���?ͬ����׼����������?
		if (get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
			/* if video _vs slave, we try to correct big delays by
			duplicating or deleting a frame */
			// ����ʱ���?
			diff = get_clock(&_vs->vidclk) - get_master_clock();

			/* skip or repeat frame. We take into account the
			delay to compute the threshold. I still don't know
			if it _vs the best guess */
			// ����ͬ��Ԥ֧
			sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
			if (!isnan(diff) && fabs(diff) < _vs->max_frame_duration) {
				// �ͺ�
				if (diff <= -sync_threshold)
					delay = FFMAX(0, delay + diff);
				else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)// ��ǰ
					delay = delay + diff;
				else if (diff >= sync_threshold)// ������������ֵ
					delay = 2 * delay;
			}
		}

		av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
			delay, -diff);

		return delay;
	}

	double MediaState::vp_duration(Frame *vp, Frame *nextvp) {
		if (vp->serial == nextvp->serial) {
			double duration = nextvp->pts - vp->pts;
			if (isnan(duration) || duration <= 0 || duration > _vs->max_frame_duration)
				return vp->duration;
			else
				return duration;
		}
		else {
			return 0.0;
		}
	}

	void MediaState::update_video_pts(double pts, int64_t pos, int serial) {
		/* update current video pts */
		set_clock(&_vs->vidclk, pts, serial);
		sync_clock_to_slave(&_vs->extclk, &_vs->vidclk);
	}

	int MediaState::queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
	{
		Frame *vp;

#if defined(DEBUG_SYNC)
		printf("frame_type=%c pts=%0.3f\n",
			av_get_picture_type_char(src_frame->pict_type), pts);
#endif

		if (!(vp = _frameQueue->frame_queue_peek_writable(&_vs->pictq)))
			return -1;

		vp->sar = src_frame->sample_aspect_ratio;
		vp->uploaded = 0;

		vp->width = src_frame->width;
		vp->height = src_frame->height;
		vp->format = src_frame->format;

		vp->pts = pts;
		vp->duration = duration;
		vp->pos = pos;
		vp->serial = serial;

		//andy
		//set_default_window_size(vp->width, vp->height, vp->sar);

		av_frame_move_ref(vp->frame, src_frame);
		_frameQueue->frame_queue_push(&_vs->pictq);
		return 0;
	}

	int MediaState::get_video_frame(AVFrame *frame)
	{
		int got_picture = decoder_decode_frame(&_vs->viddec, frame);

		if (got_picture == -2)
			return -2;

		if (got_picture < 0)
			return -1;

		if (got_picture) {
			double dpts = NAN;

			if (frame->pts != AV_NOPTS_VALUE)
				dpts = av_q2d(_vs->video_st->time_base) * frame->pts;

			frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(_vs->ic, _vs->video_st, frame);

			if (_framedrop > 0 || (_framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) {
				if (frame->pts != AV_NOPTS_VALUE) {
					double diff = dpts - get_master_clock();
					if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
						diff < 0 &&
						_vs->viddec.pkt_serial == _vs->vidclk.serial &&
						_vs->videoq.nb_packets) {
						av_frame_unref(frame);
						got_picture = 0;
					}
				}
			}
		}

		return got_picture;
	}

	int MediaState::synchronize_audio(int nb_samples)
	{
		int wanted_nb_samples = nb_samples;

		// �����������Ƶ�?��������ͨ���Ƴ������Ӳ���������ʱ��
		if (get_master_sync_type() != AV_SYNC_AUDIO_MASTER) {
			double diff, avg_diff;
			int min_nb_samples, max_nb_samples;

			// ��ȡ��Ƶʱ�Ӹ���ʱ�ӵĲ�ֵ
			diff = get_clock(&_vs->audclk) - get_master_clock();
			// �жϲ�ֵ�Ƿ���ڣ������ڷ��?����ֵ��Χ��
			if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
				// �����µĲ�ֵ
				_vs->audio_diff_cum = diff + _vs->audio_diff_avg_coef * _vs->audio_diff_cum;
				// ��¼��ֵ������
				if (_vs->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
					/* not enough measures to have a correct estimate */
					_vs->audio_diff_avg_count++;
				}
				else {
					/* estimate the A-V difference */
					// ������Ƶ����Ƶ��ʱ�Ӳ�ֵ
					avg_diff = _vs->audio_diff_cum * (1.0 - _vs->audio_diff_avg_coef);
					// �ж�ƽ����ֵ�Ƿ񳬹�����Ƶ�����ֵ�����������������µĲ����?
					if (fabs(avg_diff) >= _vs->audio_diff_threshold) {
						wanted_nb_samples = nb_samples + (int)(diff * _vs->audio_src.freq);
						min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
						max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
						wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
					}
					av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
						diff, avg_diff, wanted_nb_samples - nb_samples,
						_vs->audio_clock, _vs->audio_diff_threshold);
				}
			}
			else {
				/* too big difference : may be initial PTS errors, so
				reset A-V filter */
				// �����ֵ�������÷�ֹpts����
				_vs->audio_diff_avg_count = 0;
				_vs->audio_diff_cum = 0;
			}
		}

		return wanted_nb_samples;
	}

    bool MediaState::reOpenAudioDevice()
    {
//        printf("ffmpeg: reopen audio device!");
//        SDL_CloseAudioDevice(_audio_dev);
//        int ret = 0;
//        if ((ret = audio_open(this, channel_layout, nb_channels, sample_rate, &_vs->audio_tgt)) < 0)
//            return false;
//        SDL_PauseAudioDevice(_audio_dev, 0);
//
//        if (_vs->audio_stream >= 0) {
//            _packetQueue->packet_queue_flush(&_vs->audioq);
//            _packetQueue->packet_queue_put(&_vs->audioq, &m_flush_pkt);
//        }
        //do nothing
        return true;
    }

	/*
	int MediaState::audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
	{
		SDL_AudioSpec wanted_spec, spec;
		const char *env;
		static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
#ifdef ANDROID
        static const int next_sample_rates[] = {0, 44100, 48000};
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
			return -1;
		}
		while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
			next_sample_rate_idx--;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.silence = 0;
		wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
		wanted_spec.callback = MediaState::sdl_audio_callback;
		wanted_spec.userdata = opaque;
//#ifdef ANDROID
//        while (SDL_OpenAudio(&wanted_spec, &spec) < 0)
//#else
		while (!(_audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
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
					return -1;
				}
			}
			wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
		}
		if (spec.format != AUDIO_S16SYS) {
			av_log(NULL, AV_LOG_ERROR,
				"SDL advised audio format %d is not supported!\n", spec.format);
			return -1;
		}
		if (spec.channels != wanted_spec.channels) {
			wanted_channel_layout = av_get_default_channel_layout(spec.channels);
			if (!wanted_channel_layout) {
				av_log(NULL, AV_LOG_ERROR,
					"SDL advised channel count %d is not supported!\n", spec.channels);
				return -1;
			}
		}

		audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
		audio_hw_params->freq = spec.freq;
		audio_hw_params->channel_layout = wanted_channel_layout;
		audio_hw_params->channels = spec.channels;
		audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
		audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
		if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
			return -1;
		}
		return spec.size;
	}
	*/
	int MediaState::stream_component_open(int stream_index)
	{
		AVFormatContext *ic = _vs->ic;
		AVCodecContext *avctx;
		AVCodec *codec;
		const char *forced_codec_name = NULL;
		AVDictionary *opts = NULL;
		AVDictionaryEntry *t = NULL;

		int ret = 0;
		int stream_lowres = 0;
		uint8_t *buffer;
		int numBytes;

		if (stream_index < 0 || stream_index >= ic->nb_streams)
			return -1;

		// ��������������
		avctx = avcodec_alloc_context3(NULL);
		if (!avctx)
			return AVERROR(ENOMEM);

		// ���ƽ�������Ϣ������������
		ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
		if (ret < 0)
			goto fail;
		avctx->pkt_timebase = ic->streams[stream_index]->time_base;
		// ���ҽ�����
		codec = avcodec_find_decoder(avctx->codec_id);
		// �жϽ��������ͣ��������������������������ý�������
// 		switch (avctx->codec_type) {
// 		case AVMEDIA_TYPE_AUDIO: _vs->last_audio_stream = stream_index; forced_codec_name = audio_codec_name; break;
// 		case AVMEDIA_TYPE_SUBTITLE: _vs->last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
// 		case AVMEDIA_TYPE_VIDEO: _vs->last_video_stream = stream_index; forced_codec_name = video_codec_name; break;
// 		}
// 		if (forced_codec_name)
// 			codec = avcodec_find_decoder_by_name(forced_codec_name);
		if (!codec) {
			if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
				"No codec could be found with name '%s'\n", forced_codec_name);
			else                   av_log(NULL, AV_LOG_WARNING,
				"No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
			ret = AVERROR(EINVAL);
			goto fail;
		}
		// ���ý�������Id
		avctx->codec_id = codec->id;
		// �ж��Ƿ���Ҫ��������lowres��ֵ
		if (stream_lowres > codec->max_lowres) {
			av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder _vs %d\n",
				codec->max_lowres);
			stream_lowres = codec->max_lowres;
		}
		avctx->lowres = stream_lowres;

		//if (m_fast)
		//	avctx->flags2 |= AV_CODEC_FLAG2_FAST;

		opts = filter_codec_opts(m_codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
		if (!av_dict_get(opts, "threads", NULL, 0))
			av_dict_set(&opts, "threads", "auto", 0);
		if (stream_lowres)
			av_dict_set_int(&opts, "lowres", stream_lowres, 0);
		if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
			av_dict_set(&opts, "refcounted_frames", "1", 0);
		if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
			goto fail;
		}
		if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
			av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
			ret = AVERROR_OPTION_NOT_FOUND;
			goto fail;
		}


		_vs->eof = 0;
		ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
		switch (avctx->codec_type) {
		case AVMEDIA_TYPE_AUDIO:
			_audio_frame = av_frame_alloc();
			sample_rate = avctx->sample_rate;
			nb_channels = avctx->channels;
			channel_layout = avctx->channel_layout;

			/* prepare audio output */
            _vs->audio_buf_size = 0;
            _vs->audio_buf_index = 0;
			if ((ret = _sdlaudio->audio_open(this, channel_layout, nb_channels, sample_rate, &_vs->audio_tgt)) < 0)
				goto fail;
			_vs->audio_hw_buf_size = ret;
			_vs->audio_src = _vs->audio_tgt;

			/* init averaging filter */
			_vs->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
			_vs->audio_diff_avg_count = 0;
			/* since we do not have a precise anough audio FIFO fullness,
			we correct audio sync only if larger than this threshold */
			_vs->audio_diff_threshold = (double)(_vs->audio_hw_buf_size) / _vs->audio_tgt.bytes_per_sec;

			_vs->audio_stream = stream_index;
			_vs->audio_st = ic->streams[stream_index];
			// ��Ƶ��������ʼ��
            av_log(NULL, AV_LOG_INFO, "===== decoder_init audio =====\n");
			decoder_init(&_vs->auddec, avctx, &_vs->audioq);
			if ((_vs->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !_vs->ic->iformat->read_seek) {
				_vs->auddec.start_pts = _vs->audio_st->start_time;
				_vs->auddec.start_pts_tb = _vs->audio_st->time_base;
			}

            av_log(NULL, AV_LOG_INFO, "===== decoder_start audio =====\n");
			decoder_start(&_vs->auddec);

			_sdlaudio->pause_audiodevice(0);
			//SDL_PauseAudioDevice(_audio_dev, 0);
			break;
		case AVMEDIA_TYPE_VIDEO:
			_video_frame = av_frame_alloc();
			_displayFrame = av_frame_alloc();

			_displayFrame->format = AV_PIX_FMT_YUV420P;
			_displayFrame->width = avctx->width;
			_displayFrame->height = avctx->height;

			numBytes = avpicture_get_size((AVPixelFormat)_displayFrame->format, _displayFrame->width, _displayFrame->height);
			buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

			avpicture_fill((AVPicture*)_displayFrame, buffer, (AVPixelFormat)_displayFrame->format, _displayFrame->width, _displayFrame->height);

			_vs->video_stream = stream_index;
			_vs->video_st = ic->streams[stream_index];
			// ��Ƶ��������ʼ��
            av_log(NULL, AV_LOG_INFO, "===== decoder_init video =====\n");
			decoder_init(&_vs->viddec, avctx, &_vs->videoq);
			
			_tb = _vs->video_st->time_base;
			_video_frame_rate = av_guess_frame_rate(_vs->ic, _vs->video_st, NULL);
                
            av_log(NULL, AV_LOG_INFO, "===== decoder_start video =====\n");
			decoder_start(&_vs->viddec);

			_vs->queue_attachments_req = 1;
			break;
		default:
			break;
		}
		goto out;

	fail:
		avcodec_free_context(&avctx);
	out:
		av_dict_free(&opts);

		return ret;
	}

	/********************************************read tread*************************************************/
	int MediaState::decode_interrupt_cb(void *ctx)
	{
		VideoState *_vs = (VideoState*)ctx;
        if (1 == _vs->abort_request)
            av_log(NULL, AV_LOG_INFO, "decode_interrupt_cb: %d!!!!!!!!!!!\n", _vs->abort_request);
        if ((av_gettime() - _vs->_last_frametime) > 15000000) {
            _vs->abort_request = 1;
        }
		return _vs->abort_request;
	}

	int MediaState::stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
		return stream_id < 0 ||
			queue->abort_request ||
			(st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
			(queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0));
	}

	int MediaState::is_realtime(AVFormatContext *s)
	{
		if (!strcmp(s->iformat->name, "rtp")
			|| !strcmp(s->iformat->name, "rtsp")
			|| !strcmp(s->iformat->name, "sdp")
			)
			return 1;

		if (s->pb && s->url && (!strncmp(s->url, "rtp:", 4)
			|| !strncmp(s->url, "udp:", 4)
			)
			)
			return 1;
		return 0;
	}

	double MediaState::getDuration()
	{
		_main_mutex.lock();
		if (_vs->ic == NULL) return -1;
		int64_t duration = _vs->ic->duration;// +(_vs->ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
		double secs = (double)duration / (double)AV_TIME_BASE;
		_main_mutex.unlock();
		return secs;
	}

	double MediaState::mediaDuration()
	{
		if (_vs->ic == NULL) return -1;
		int64_t duration = _vs->ic->duration;// +(_vs->ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
		double secs = (double)duration / (double)AV_TIME_BASE;
		return secs;
	}

	void MediaState::pause()
	{
		//mute();
		pushEvent( FF_PAUSE_EVENT );
	}

	void MediaState::resume()
	{
		//unmute();
		pushEvent( FF_RESUME_EVENT );
	}
	
	bool MediaState::isPaused()
	{
		_main_mutex.lock();
		bool paused = _vs->paused;
		_main_mutex.unlock();
		return paused;
	}

	void MediaState::mute()
	{
		//_vs->audio_volume = 0;
        //_vs->muted = true;
		pushEvent( FF_MUTE_EVENT );
	}

	void MediaState::unmute()
	{
//        _vs->muted = false;
//        _vs->audio_volume = 128;
		pushEvent( FF_UNMUTE_EVENT );
	}

	/*******************************************************************************************************************/
	void MediaState::init_vs()
	{
		_vs->paused = false;
		_vs->audio_buf = NULL;
		_vs->audio_buf1 = NULL;
		_vs->swr_ctx = NULL;
		_vs->audio_stream = -1;
		_vs->video_stream = -1;
		_vs->img_convert_ctx = NULL;
        _vs->_last_frametime = av_gettime();
	}

	VideoState *MediaState::stream_open(char *filebuffer, int64_t filebuffersize)
	{
		_vs = (VideoState*)av_mallocz(sizeof(VideoState));
		if (!_vs)
			return NULL;
		_vs->filename = NULL;
		_vs->filebuffer = filebuffer;
		_vs->filebuffersize = filebuffersize;
		_vs->filesize = filebuffersize;
		_vs->filepos = 0;
		_vs->iformat = NULL;
		_vs->seek_flag_audio = 0;
		_vs->seek_flag_video = 0;
		init_vs();

		/* start video display */
		if (_frameQueue->frame_queue_init(&_vs->pictq, &_vs->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "===== video frame queue init error =====\n");
			goto fail;
        }
		if (_frameQueue->frame_queue_init(&_vs->sampq, &_vs->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "===== audio frame queue init error =====\n");
			goto fail;
        }

		if (_packetQueue->packet_queue_init(&_vs->videoq) < 0 ||
			_packetQueue->packet_queue_init(&_vs->audioq) < 0 )
        {
            av_log(NULL, AV_LOG_ERROR, "===== packet queue init error =====\n");
			goto fail;
        }

		init_clock(&_vs->vidclk, &_vs->videoq.serial);
		init_clock(&_vs->audclk, &_vs->audioq.serial);
		init_clock(&_vs->extclk, &_vs->extclk.serial);
		_vs->audio_clock_serial = -1;
		if (m_startup_volume < 0)
			av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", m_startup_volume);
		if (m_startup_volume > 100)
			av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", m_startup_volume);
		m_startup_volume = av_clip(m_startup_volume, 0, 100);
		m_startup_volume = av_clip(SDL_MIX_MAXVOLUME * m_startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
		_vs->audio_volume = m_startup_volume;
		_muted = false;
		_vs->av_sync_type = m_av_sync_type;
		
		if(!init_media()){
		fail:
			stream_close();
            av_log(NULL, AV_LOG_ERROR, "===== stream_open error =====\n");
			return NULL;
		}
		return _vs;
	}

	VideoState *MediaState::stream_open(const char *filename, AVInputFormat *iformat)
	{
		_vs = (VideoState*)av_mallocz(sizeof(VideoState));
		if (!_vs)
			return NULL;
		_vs->filename = av_strdup(filename);
		if (!_vs->filename)
			goto fail;
		_vs->filebuffer = NULL;
		_vs->iformat = iformat;
		init_vs();

		/* start video display */
		if (_frameQueue->frame_queue_init(&_vs->pictq, &_vs->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "===== video frame queue init error =====\n");
            goto fail;
        }
        if (_frameQueue->frame_queue_init(&_vs->sampq, &_vs->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "===== audio frame queue init error =====\n");
            goto fail;
        }

        if (_packetQueue->packet_queue_init(&_vs->videoq) < 0 ||
            _packetQueue->packet_queue_init(&_vs->audioq) < 0 )
        {
            av_log(NULL, AV_LOG_ERROR, "===== packet queue init error =====\n");
            goto fail;
        }

		init_clock(&_vs->vidclk, &_vs->videoq.serial);
		init_clock(&_vs->audclk, &_vs->audioq.serial);
		init_clock(&_vs->extclk, &_vs->extclk.serial);
		_vs->audio_clock_serial = -1;
		if (m_startup_volume < 0)
			av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", m_startup_volume);
		if (m_startup_volume > 100)
			av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", m_startup_volume);
		m_startup_volume = av_clip(m_startup_volume, 0, 100);
		m_startup_volume = av_clip(SDL_MIX_MAXVOLUME * m_startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
		_vs->audio_volume = m_startup_volume;
		_muted = false;
		_vs->av_sync_type = m_av_sync_type;

		if (!init_media()) {
		fail:
			stream_close();
            av_log(NULL, AV_LOG_ERROR, "===== stream_open error =====\n");
			return NULL;
		}
		return _vs;
	}

	void MediaState::refresh_loop_wait_event()
	{
		_remaining_time = 0.0;
		if (_remaining_time > 0.0)
			av_usleep((int64_t)(_remaining_time * 1000000.0));
		_remaining_time = REFRESH_RATE;
		if (_vs->show_mode != VideoState::ShowMode::SHOW_MODE_NONE && (!_vs->paused || _vs->force_refresh))
			video_refresh(&_remaining_time);
	}

	void MediaState::video_refresh(double *remaining_time)
	{
		double time;

		//Frame *sp, *sp2;

		if (!_vs->paused && get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && _vs->realtime)
			check_external_clock_speed();

		if (_vs->show_mode != VideoState::ShowMode::SHOW_MODE_VIDEO && _vs->audio_st) {
			time = av_gettime_relative() / 1000000.0;
			if (_vs->force_refresh || _vs->last_vis_time + _rdftspeed < time) {
				video_display();
				_vs->last_vis_time = time;
			}
			*remaining_time = FFMIN(*remaining_time, _vs->last_vis_time + _rdftspeed - time);
		}

		if (_vs->video_st) {
		retry:
			if (_frameQueue->frame_queue_nb_remaining(&_vs->pictq) == 0) {
				// nothing to do, no picture to display in the queue
				//av_log(NULL, AV_LOG_WARNING, "nothing to do, no picture to display in the queue\n");
			}
			else {
				double last_duration, duration, delay;
				Frame *vp, *lastvp;

				/* dequeue the picture */
				lastvp = _frameQueue->frame_queue_peek_last(&_vs->pictq);
				vp = _frameQueue->frame_queue_peek(&_vs->pictq);

				if (vp->serial != _vs->videoq.serial) {
					_frameQueue->frame_queue_next(&_vs->pictq);
					goto retry;
				}

				if (lastvp->serial != vp->serial)
					_vs->frame_timer = av_gettime_relative() / 1000000.0;

				if (_vs->paused && _getFirstPicture)
					goto display;

				/* compute nominal last_duration */
				last_duration = vp_duration(lastvp, vp);
				delay = compute_target_delay(last_duration);

				time = av_gettime_relative() / 1000000.0;
				if (time < _vs->frame_timer + delay) {
					*remaining_time = FFMIN(_vs->frame_timer + delay - time, *remaining_time);
					goto display;
				}

				_vs->frame_timer += delay;
				if (delay > 0 && time - _vs->frame_timer > AV_SYNC_THRESHOLD_MAX)
					_vs->frame_timer = time;

				SDL_LockMutex(_vs->pictq.mutex);
				if (!isnan(vp->pts))
					update_video_pts(vp->pts, vp->pos, vp->serial);
				SDL_UnlockMutex(_vs->pictq.mutex);

				if (_frameQueue->frame_queue_nb_remaining(&_vs->pictq) > 1) {
					Frame *nextvp = _frameQueue->frame_queue_peek_next(&_vs->pictq);
					duration = vp_duration(vp, nextvp);
					if (!_vs->step && (_framedrop > 0 || (_framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) && time > _vs->frame_timer + duration) {
						_frameQueue->frame_queue_next(&_vs->pictq);
						goto retry;
					}
				}

				_frameQueue->frame_queue_next(&_vs->pictq);
				_vs->force_refresh = 1;

				if (_vs->step && !_vs->paused)// && get_master_clock() > 0.01)
				{
					//stream_toggle_pause();  �޸� step��������Ϊ0��bug
					toggle_pause();
					video_display();
					av_log(NULL, AV_LOG_WARNING,
						"step_to_next_frame done, resume to pause state\n");
				}
				_getFirstPicture = true;
			}
		display:
			/* display picture */
			if (_vs->force_refresh && _vs->show_mode == VideoState::ShowMode::SHOW_MODE_VIDEO && _vs->pictq.rindex_shown)
				video_display();
		}
		_vs->force_refresh = 0;
	}

	void MediaState::video_display()
	{
		if (_vs->video_st)
			video_image_display();
	}

	void MediaState::video_image_display()
	{
		Frame *vp; //��Ƶ֡
		//Frame *sp = NULL;

		vp = _frameQueue->frame_queue_peek_last(&_vs->pictq);

		if (!vp->uploaded) {
			if (set_texture_buf(vp->frame, &_vs->img_convert_ctx) < 0)
				return;
			vp->uploaded = 1;
		}
	}

	//����ͼ������
	int MediaState::set_texture_buf(AVFrame *frame, struct SwsContext **img_convert_ctx)
	{
		*img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
			frame->width, frame->height, (AVPixelFormat)frame->format, _displayFrame->width, _displayFrame->height,
			(AVPixelFormat)_displayFrame->format, SWS_BILINEAR, NULL, NULL, NULL);
		if (*img_convert_ctx != NULL)
		{
			sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
				0, frame->height, _displayFrame->data, _displayFrame->linesize);
            
            unsigned int len = (_displayFrame->width * _displayFrame->height * 3) >> 1;
            
            if(len != _I420_datasize && 0 != _I420_datasize)
            {
                _picture_size_change = true;
            }
            
            _I420_datasize = len;
            
            _picture_fresh_count++;
            
            _buff_change = true;
            
			return 0;
		}

		return -1;
	}

	uint8_t *MediaState::getData(unsigned int& refresh_count)
	{
        //std::unique_lock<std::mutex> lk(_main_mutex);
		if (_exit)
		{
			return NULL;
		}
		_main_mutex.lock();
		
        if (refresh_count < _picture_fresh_count) {
            
            refresh_count = _picture_fresh_count;
            
            if(_buff_change)
            {
                _buff_change = false;
                int w = _displayFrame->width;
                int h = _displayFrame->height;
                
                if (NULL == _displayFrame) {
                    _main_mutex.unlock();
                    return NULL;
                }
                int len = _I420_datasize * sizeof(uint8_t);
                if (_I420 != NULL && _picture_size_change) {
                    _picture_size_change = false;
                    av_free(_I420);
                    _I420 = (uint8_t *)av_malloc(len);
                }
                if (_I420 == NULL)
                    _I420 = (uint8_t *)av_malloc(len);

                memset(_I420, 0, len);
                memcpy(_I420, *_displayFrame->data, len);
                
                _main_mutex.unlock();
                
                if (_rgba == NULL) {
                    _rgba = (uint8_t *)av_malloc(w * h * 4 * sizeof(uint8_t));
                }
                memset(_rgba, 0, w * h * 4 * sizeof(uint8_t));
                
                libyuv::I420ToABGR(&_I420[0],
                w,
                &_I420[w * h],
                w >> 1,
                &_I420[(w * h * 5) >> 2],
                w >> 1,
                _rgba,
                w * 4,
                w,
                h);
                
                return _rgba;
            }
            else if (_event == FF_OVER)
            {
                _main_mutex.unlock();
                return _rgba;
            }
            else
            {
                _main_mutex.unlock();
                return NULL;
            }
            
        }else{
			_main_mutex.unlock();
            return NULL;
        }
	}

	unsigned int MediaState::getWidth()
	{
		return _displayFrame->width;
	}

	unsigned int MediaState::getHeight()
	{
		return _displayFrame->height;
	}

	/******************************************************************************************************************************************************************/
	void MediaState::decoder_start(Decoder *d)
	{
		_packetQueue->packet_queue_start(d->queue);
	}

	AVDictionary **MediaState::setup_find_stream_info_opts(AVFormatContext *s,
		AVDictionary *codec_opts)
	{
		int i;
		AVDictionary **opts;

		if (!s->nb_streams)
			return NULL;
		// opts = (AVDictionary **)av_mallocz_array(s->nb_streams, sizeof(*opts)); /* for 4.0+ version*/
		opts = (AVDictionary **)av_calloc(s->nb_streams, sizeof(*opts));
		if (!opts) {
			av_log(NULL, AV_LOG_ERROR,
				"Could not alloc memory for stream options.\n");
			return NULL;
		}
		for (i = 0; i < s->nb_streams; i++)
			opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id,
				s, s->streams[i], NULL);
		return opts;
	}

	AVDictionary *MediaState::filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
		AVFormatContext *s, AVStream *st, AVCodec *codec)
	{
		AVDictionary    *ret = NULL;
		AVDictionaryEntry *t = NULL;
		int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
			: AV_OPT_FLAG_DECODING_PARAM;
		char          prefix = 0;
		const AVClass    *cc = avcodec_get_class();

		if (!codec)
			codec = s->oformat ? avcodec_find_encoder(codec_id)
			: avcodec_find_decoder(codec_id);
		if (!codec)
			return NULL;

		switch (codec->type) {
		case AVMEDIA_TYPE_VIDEO:
			prefix = 'v';
			flags |= AV_OPT_FLAG_VIDEO_PARAM;
			break;
		case AVMEDIA_TYPE_AUDIO:
			prefix = 'a';
			flags |= AV_OPT_FLAG_AUDIO_PARAM;
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			prefix = 's';
			flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
			break;
		default:
			break;
		}

		while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
			char *p = strchr(t->key, ':');

			/* check stream specification in opt name */
			if (p)
				switch (check_stream_specifier(s, st, p + 1)) {
				case  1: *p = 0; break;
				case  0:         continue;
				default:         return NULL;
				}

			if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
				(codec && codec->priv_class &&
					av_opt_find(&codec->priv_class, t->key, NULL, flags,
						AV_OPT_SEARCH_FAKE_OBJ)))
				av_dict_set(&ret, t->key, t->value, 0);
			else if (t->key[0] == prefix &&
				av_opt_find(&cc, t->key + 1, NULL, flags,
					AV_OPT_SEARCH_FAKE_OBJ))
				av_dict_set(&ret, t->key + 1, t->value, 0);

			if (p)
				*p = ':';
		}
		return ret;
	}

	int MediaState::check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
	{
		int ret = avformat_match_stream_specifier(s, st, spec);
		if (ret < 0)
			av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
		return ret;
	}

	/***********************************************************************************************************/

	uint8_t *MediaState::getThumbnail()
	{
		AVFrame *frame = av_frame_alloc();
		AVFrame *frameYUV = av_frame_alloc();
		frameYUV->format = AV_PIX_FMT_YUV420P;
		frameYUV->width = _displayFrame->width;
		frameYUV->height = _displayFrame->height;
		int numBytes = avpicture_get_size((AVPixelFormat)frameYUV->format, frameYUV->width, frameYUV->height);
		uint8_t * buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

		avpicture_fill((AVPicture*)frameYUV, buffer, (AVPixelFormat)frameYUV->format, frameYUV->width, frameYUV->height);

		AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));

		struct SwsContext *img_convert_ctx = sws_getContext(_displayFrame->width, _displayFrame->height, (AVPixelFormat)_displayFrame->format, _displayFrame->width, _displayFrame->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
	
		bool success = false;
		while (av_read_frame(_vs->ic, packet) >= 0) {
			if (packet->stream_index == _vs->video_stream)
			{
				if (avcodec_send_packet(_vs->viddec.avctx, packet) == AVERROR(EAGAIN))
					goto end;

				int ret = AVERROR(EAGAIN);
				
				do 
				{
					if (_vs->viddec.avctx->codec_type == AVMEDIA_TYPE_VIDEO)
					{
						ret = avcodec_receive_frame(_vs->viddec.avctx, frame);
						if (ret>=0)
						{
							sws_scale(img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, frameYUV->data, frameYUV->linesize);

							uint8_t	*_bgra = (uint8_t *)av_malloc(_displayFrame->width * _displayFrame->height * 4 * sizeof(uint8_t));

							memset(_bgra, 0, _displayFrame->width * _displayFrame->height * 4 * sizeof(uint8_t));

							libyuv::I420ToRGBA(frameYUV->data[0],
								frameYUV->linesize[0],
								frameYUV->data[1],
								frameYUV->linesize[1],
								frameYUV->data[2],
								frameYUV->linesize[2],
								_bgra,
								frameYUV->width * 4,
								frameYUV->width,
								frameYUV->height);

							if (_rgba == NULL)
								_rgba = (uint8_t *)av_malloc(_displayFrame->width * _displayFrame->height * 4 * sizeof(uint8_t));

							memset(_rgba, 0, _displayFrame->width * _displayFrame->height * 4 * sizeof(uint8_t));

							libyuv::ARGBToBGRA(_bgra,
								frameYUV->width * 4,
								_rgba,
								frameYUV->width * 4,
								frameYUV->width,
								frameYUV->height);

							libyuv::ARGBToI420(_rgba,
								frameYUV->width * 4,
								frameYUV->data[0],
								frameYUV->linesize[0],
								frameYUV->data[1],
								frameYUV->linesize[1],
								frameYUV->data[2],
								frameYUV->linesize[2],
								frameYUV->width,
								frameYUV->height);
							success = true;
                            av_free(_bgra);
							break;
						}
						else
						{
							break;
						}
					}
				} while (ret != AVERROR(EAGAIN));
				if (success)
				{
					av_free_packet(packet);
					break;
				}
			}
			av_free_packet(packet);
		}

	end:
		sws_freeContext(img_convert_ctx);
		av_free(frameYUV->data[0]);
		av_frame_free(&frameYUV);
		av_frame_free(&frame);

		int ret = avformat_seek_file(_vs->ic, -1, INT64_MIN, 0, INT64_MAX, 0);
		if (ret < 0) {
			av_log(NULL, AV_LOG_WARNING, "could not seek to position %0.3f\n",
				(double)0 / AV_TIME_BASE);
		}
		return _rgba;
	}

	void MediaState::event_loop()
	{
        
		frame_loop();
		audio_loop();
		video_loop();

		if (_event == FF_HOLD || _event == FF_OVER)
		{
			return;
		}

		refresh_loop_wait_event();
		
        //std::unique_lock<std::mutex> lk(_event_mutex);
        
		if (_event_queue.empty())
		{
			return;
		}

		do 
		{
            _event_mutex.lock();
			FFEvent *event = _event_queue.front();
            _event_mutex.unlock();
            
			int type = event->type;
			switch (type) {
			case FF_PAUSE_EVENT:
            {
				if (1 == _vs->step)
				{
					/*step ==1 ˵����֮ǰ��seek�������������ڱ�pause״̬�½��еģ�seek��step��Ϊ1��
					pause״̬������Ϊfalse�Ա㽫��Ƶ֡��ת����ȷ���沢��ȡbuffer��֮���ٽ�pasue��Ϊtrue
					�����ʱ�û��ٵ���pasue��ʵ���ظ���ͣ������������Ч����
					*/
//					av_log(NULL, AV_LOG_ERROR,
//						"Media already paused and seek,dont repeat your pause action!!!!!!!!!!!\n");
				}
				else if (!_vs->paused)
				{
                    mute();
					toggle_pause();
				}
				else
				{
//					av_log(NULL, AV_LOG_WARNING,
//						"Media already paused\n");
				}
				_event = FF_PAUSE;
            }
				break;
			case FF_RESUME_EVENT:
            {
				if (1 == _vs->step)
				{
					/*��ʱ����seek״̬��pause��������Ϊfalse��stepΪ1����ʵ���Ǵ���pause״̬
					  ��step�ָ�Ĭ��ֵ
					*/
                    unmute();
					toggle_pause();//������ͣ״̬����
					toggle_pause();//����ȡ����ͣ״̬
//					av_log(NULL, AV_LOG_WARNING,
//						"Media already paused and seek, force resume!!!!!!!!!!!\n");
				}
				else if (_vs->paused)
				{
                    unmute();
					toggle_pause();
				}
				else 
				{
//					av_log(NULL, AV_LOG_WARNING,
//						"Media already resumed\n");
				}
				_event = FF_PLAY;
            }
				break;
			case FF_MUTE_EVENT:
            {
				//toggle_mute();
                _vs->audio_volume = 0;
                _muted = true;
                _event = FF_PLAY;
				//_event = _vs->muted ? FF_MUTE : FF_PLAY;
            }
				break;
            case FF_UNMUTE_EVENT:
            {
                _muted = false;
                _vs->audio_volume = 128;
                _event = FF_PLAY;
            }
                break;
			case FF_OVER_EVENT:
				_event = FF_OVER;
				//do_exit();
				break;
            case FF_ERROR_EVENT:
            {
                _event = FF_PLAY;
                double seconds = get_master_clock();
                int64_t seek_time = (int64_t)(seconds * AV_TIME_BASE);
                if (m_start_time != AV_NOPTS_VALUE && (m_start_time - seek_time)>1000000)
                    seek_time = m_start_time;
                av_log(NULL, AV_LOG_WARNING, "********** unexcept endding reseek to %0.3f(%lld) continue **********\n", seconds, seek_time);
                stream_seek(seek_time, 0, 0, true);
            }
                break;
			case SDL_QUIT:
			case FF_QUIT_EVENT:
            {
				_event = FF_QUIT;
				do_exit();
            }
				break;
			case FF_SEEK_EVENT:
			{
				double seconds = event->data;
//				av_log(NULL, AV_LOG_WARNING, "event seek to position %0.3f\n",
//					seconds);
				//double pos = get_master_clock();

				//pos += seconds;
				
				//stream_seek(av_rescale(seconds * 1000, AV_TIME_BASE, 1000), 0, 0, true);
                const double eps = 1e-6;
                double dur = mediaDuration();
                if (seconds > dur + eps) {
                    _skip_to_last = true;
                    seconds = dur;
                }
				stream_seek((int64_t)(seconds * AV_TIME_BASE), 0, 0, true);// (int64_t)(seconds * AV_TIME_BASE)

				_event = FF_PLAY;
			}
			break;
			default:
				_event = FF_PLAY;
			}

            _event_mutex.lock();
			_event_queue.pop();
			delete event;
            _event_mutex.unlock();

			if (_event == FF_QUIT)
			{
				break;
			}

		} while (!_event_queue.empty());

	}

/////////////////////////////////////////////no thread///////////////////////////////////////////////////////////////////

	bool MediaState::init_media()
	{
		AVFormatContext *ic = NULL;
		int err, i, ret=-1;
		int st_index[AVMEDIA_TYPE_NB];
		AVDictionaryEntry *t;
		int scan_all_pmts_set = 0;
		
		// ���ó�ʼֵ
		memset(st_index, -1, sizeof(st_index));

		_vs->eof = 0;

		// ��������������
		ic = avformat_alloc_context();
		if (!ic) {
			av_log(NULL, AV_LOG_FATAL, "===== Could not allocate context. =====\n");
			ret = AVERROR(ENOMEM);
			goto end;
		}
		// ���ý����жϻص�����
		ic->interrupt_callback.callback = decode_interrupt_cb;
		// �����жϻص�����
		ic->interrupt_callback.opaque = _vs;
		// ��ȡ����
		if (!av_dict_get(m_format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
			av_dict_set(&m_format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
			scan_all_pmts_set = 1;
		}
		// ���ļ�
		if (_vs->filename != NULL && !strncmp(_vs->filename, "http", 4))
		{
			av_dict_set(&m_format_opts, "timeout", "15000000", 0);//���ó�ʱ20��
            av_dict_set(&m_format_opts, "reconnect_streamed", "1", 0);
            av_dict_set(&m_format_opts, "reconnect_delay_max", "20", 0);
			_vs->network_video = true;
		}
		else 
		{
			_vs->network_video = false;
		}
		if (_vs->filename != NULL)
		{
			err = avformat_open_input(&ic, _vs->filename, _vs->iformat, &m_format_opts);
			if (err < 0) {
				av_log(NULL, AV_LOG_FATAL, "===== Could not open input %s. ===== \n", _vs->filename);
				ret = -1;
				goto end;
			}
		}
		else if (_vs->filebuffer != NULL)
		{
			unsigned char *aviobuffer = (unsigned char *)av_malloc(32768);
			AVIOContext *avio = avio_alloc_context(aviobuffer, 32768, 0, this, read_buffer, NULL, seek_buffer);
			ic->pb = avio;
			ic->flags = AVFMT_FLAG_CUSTOM_IO;
			err = avformat_open_input(&ic, NULL, 0, 0);
			if (err != 0)
			{
				av_log(NULL, AV_LOG_FATAL, "===== Could not open input filebuffer. =====\n");
				ret = -1;
				goto end;
			}
		}
		else
		{
			goto end;
		}

		if (scan_all_pmts_set)
			av_dict_set(&m_format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

		if ((t = av_dict_get(m_format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
			av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
			ret = AVERROR_OPTION_NOT_FOUND;
			goto end;
		}
		_vs->ic = ic;

		av_format_inject_global_side_data(ic);

		do {
			AVDictionary **opts = setup_find_stream_info_opts(ic, m_codec_opts);
			int orig_nb_streams = ic->nb_streams;

			err = avformat_find_stream_info(ic, opts);

			for (i = 0; i < orig_nb_streams; i++)
				av_dict_free(&opts[i]);
			av_freep(&opts);

			if (err < 0) {
				av_log(NULL, AV_LOG_WARNING,
					"===== could not find codec parameters =====\n");
				ret = -1;
				goto end;
			}
		} while (0);

		if (ic->pb)
			ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

		//if (m_seek_by_bytes < 0)
		//	m_seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

		_vs->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

		/* if seeking requested, we execute it */
		if (m_start_time != AV_NOPTS_VALUE) {
			int64_t timestamp;

			timestamp = m_start_time;
			/* add the stream start time */
			if (ic->start_time != AV_NOPTS_VALUE)
				timestamp += ic->start_time;
			// ��λ�ļ�
			ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
			if (ret < 0) {
				av_log(NULL, AV_LOG_WARNING, "===== could not seek to position %0.3f =====\n",
					(double)timestamp / AV_TIME_BASE);
			}
			else {
                _vs->seek_time = m_start_time / AV_TIME_BASE;
                _vs->seek_flag_audio = 1;
                _vs->seek_flag_video = 1;
				av_log(NULL, AV_LOG_WARNING, "===== start with position %0.3f =====\n",
					(double)timestamp / AV_TIME_BASE);
			}
		}

		_vs->realtime = is_realtime(ic);

#ifdef FFMPEG_DUMP
		av_dump_format(ic, 0, _vs->filename, 0);
#endif // FFMPEG_DUMP
		
		// ��ȡ������Ӧ������
		for (i = 0; i < ic->nb_streams; i++) {
			AVStream *st = ic->streams[i];
			enum AVMediaType type = st->codecpar ? st->codecpar->codec_type : st->codec->codec_type;
			st->discard = AVDISCARD_ALL;
			if (type >= 0 && m_wanted_stream_spec[type] && st_index[type] == -1)
				if (avformat_match_stream_specifier(ic, st, m_wanted_stream_spec[type]) > 0)
					st_index[type] = i;
		}
		for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
			if (m_wanted_stream_spec[i] && st_index[i] == -1) {
				av_log(NULL, AV_LOG_ERROR, "===== Stream specifier %s does not match any %s stream =====\n", m_wanted_stream_spec[i], av_get_media_type_string((AVMediaType)i));
				st_index[i] = INT_MAX;
			}
		}

		// ������Ƶ��
		st_index[AVMEDIA_TYPE_VIDEO] =
			av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
				st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

		// ������Ƶ��
		st_index[AVMEDIA_TYPE_AUDIO] =
			av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
				st_index[AVMEDIA_TYPE_AUDIO],
				st_index[AVMEDIA_TYPE_VIDEO],
				NULL, 0);

		// ������ʾģʽ
		_vs->show_mode = m_show_mode;

		/* open the streams */
		if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
			ret = stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
			if (ret < 0)
				goto end;
		}

		ret = -1;
		if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
			ret = stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
		}
		if (_vs->show_mode == VideoState::ShowMode::SHOW_MODE_NONE)
			_vs->show_mode = ret >= 0 ? VideoState::ShowMode::SHOW_MODE_VIDEO : VideoState::ShowMode::SHOW_MODE_RDFT;

		// �����Ƶ������Ƶ���������ڣ����˳�?
		if (_vs->video_stream < 0 && _vs->audio_stream < 0) {
			av_log(NULL, AV_LOG_FATAL, "===== Failed to open file or configure filtergraph ===== \n");
			ret = -1;
			goto end;
		}

		if (m_infinite_buffer < 0 && _vs->realtime)
			m_infinite_buffer = 1;


		_duration = mediaDuration();

		ret = 0;
	end:
		if (!_vs->ic)
		{
			avformat_close_input(&ic);
			return false;
		}
		if (ret!=0)
		{
			return false;
		}
		return true;
	}

	void MediaState::frame_loop()
	{
		if (!_play)
			return;

		if (_vs->abort_request)
			return;
		if (_vs->paused != _vs->last_paused) {
			_vs->last_paused = _vs->paused;
			if (_vs->paused)
				_vs->read_pause_return = av_read_pause(_vs->ic);
			else
				av_read_play(_vs->ic);
		}

		int ret = -1;
		AVPacket *pkt = &m_read_pkt;
		int64_t stream_start_time;
		int64_t pkt_ts;
		int pkt_in_play_range = 0;

		// ��λ�ļ�
		if (_vs->seek_req) {
			int64_t seek_target = _vs->seek_pos;
			int64_t seek_min = _vs->seek_rel > 0 ? seek_target - _vs->seek_rel + 2 : INT64_MIN;
			int64_t seek_max = _vs->seek_rel < 0 ? seek_target - _vs->seek_rel - 2 : INT64_MAX;
			// FIXME the +-2 _vs due to rounding being not done in the correct direction in generation
			//      of the seek_pos/seek_rel variables
			//_vs->seek_flags = AVSEEK_FLAG_ANY;
			ret = avformat_seek_file(_vs->ic, -1, seek_min, seek_target, seek_max, _vs->seek_flags);
			//ret = av_seek_frame(_vs->ic, -1, seek_target, AVSEEK_FLAG_BACKWARD);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR,
					"%s: error while seeking\n", _vs->ic->url);
			}
			else {
				// ��ն���? ѹ��ˢ�±��flush_pkt
				if (_vs->audio_stream >= 0) {
					_packetQueue->packet_queue_flush(&_vs->audioq);
					_packetQueue->packet_queue_put(&_vs->audioq, &m_flush_pkt);
				}

				// ��ն���? ѹ��ˢ�±��flush_pkt
				if (_vs->video_stream >= 0) {
					_packetQueue->packet_queue_flush(&_vs->videoq);
					_packetQueue->packet_queue_put(&_vs->videoq, &m_flush_pkt);
				}
				// ���ݶ�λ�ı�־����ʱ��
				if (_vs->seek_flags & AVSEEK_FLAG_BYTE) {
					set_clock(&_vs->extclk, NAN, 0);
				}
				else {
					set_clock(&_vs->extclk, seek_target / (double)AV_TIME_BASE, 0);
				}
			}

			/**��׼seek**/
			_vs->seek_time = _vs->seek_pos / AV_TIME_BASE;
			_vs->seek_flag_audio = 1;
			_vs->seek_flag_video = 1;
			/**��׼seek**/

			_vs->seek_req = 0;
			_vs->queue_attachments_req = 1;
			_vs->eof = 0;
			if (_vs->paused)
				step_to_next_frame();
		}
		if (_vs->queue_attachments_req) {
			if (_vs->video_st && _vs->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				AVPacket copy = { 0 };
				if ((ret = av_packet_ref(&copy, &_vs->video_st->attached_pic)) < 0)
				{
					av_log(NULL, AV_LOG_ERROR, "error while call av_packet_ref\n");
					pushEvent( FF_QUIT_EVENT );
					return;
				}
				_packetQueue->packet_queue_put(&_vs->videoq, &copy);
				_packetQueue->packet_queue_put_nullpacket(&_vs->videoq, _vs->video_stream);
			}
			_vs->queue_attachments_req = 0;
		}

		/* if the queue are full, no need to read more */
		// ����������д�����ʧ�ܣ����Ҵ��������ݻ����㹻�İ�ʱ���ȴ���������е��������ĵ�
		if (m_infinite_buffer < 1 &&
			(_vs->audioq.size + _vs->videoq.size > MAX_QUEUE_SIZE
				|| (stream_has_enough_packets(_vs->audio_st, _vs->audio_stream, &_vs->audioq) &&
					stream_has_enough_packets(_vs->video_st, _vs->video_stream, &_vs->videoq) ))) {
			return;
		}
		if (!_vs->paused &&
			(!_vs->audio_st || (_vs->auddec.finished == _vs->audioq.serial && _frameQueue->frame_queue_nb_remaining(&_vs->sampq) == 0)) &&
			(!_vs->video_st || (_vs->viddec.finished == _vs->videoq.serial && _frameQueue->frame_queue_nb_remaining(&_vs->pictq) == 0))) {
			if (m_loop != 1 && (!m_loop || --m_loop)) {
				_vs->auddec.finished = 0;
				_vs->viddec.finished = 0;
				_getFirstPicture = false;
                _skip_to_last = false;
				stream_seek(m_start_time != AV_NOPTS_VALUE ? m_start_time : 0, 0, 0);
			}
			else {
				//���Ž���
				if (_event == FF_OVER)
				{
					return;
				}
				double dur = mediaDuration();
				double cur = get_master_clock();
				if (fabs(dur - cur) < 1)
				{
					pushEvent(FF_OVER_EVENT);
				}
                else if (!_vs->abort_request && _vs->network_video)
                {
                    pushEvent( FF_ERROR_EVENT );//cancle auto end
                    av_log(NULL, AV_LOG_WARNING, "********** may be unexcept endding **********\n");
				}
				else 
				{
					pushEvent(FF_OVER_EVENT);
				}
				return;
			}
		}
		// ��ȡ���ݰ�
		ret = av_read_frame(_vs->ic, pkt);
		if (ret < 0) {
			// ��ȡ������ʧ��
            av_log(NULL, AV_LOG_WARNING, "unexcept av_read_frame return id:%d \n", this->m_uniqueID);
			if ((ret == AVERROR_EOF || avio_feof(_vs->ic->pb)) && !_vs->eof) {
				if (_vs->video_stream >= 0)
					_packetQueue->packet_queue_put_nullpacket(&_vs->videoq, _vs->video_stream);
				if (_vs->audio_stream >= 0)
					_packetQueue->packet_queue_put_nullpacket(&_vs->audioq, _vs->audio_stream);
				_vs->eof = 1;

			}
			if (_vs->ic->pb && _vs->ic->pb->error)
			{
				av_log(NULL, AV_LOG_WARNING, "got a error frame \n");
                av_packet_unref(pkt);
                //_vs->abort_request = 1;
                _got_error_frame = true;
				return;
			}
			return;
		}
		else {
			_vs->eof = 0;
		}
        _got_error_frame = false;
        _vs->_last_frametime = av_gettime();
		/* check if packet _vs in play range specified by user, then queue, otherwise discard */
		stream_start_time = _vs->ic->streams[pkt->stream_index]->start_time;
		pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
		pkt_in_play_range = m_duration == AV_NOPTS_VALUE ||
			(pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
			av_q2d(_vs->ic->streams[pkt->stream_index]->time_base) -
			(double)(m_start_time != AV_NOPTS_VALUE ? m_start_time : 0) / 1000000
			<= ((double)m_duration / 1000000);
		// ���⸴�õõ������ݰ���ӵ���Ӧ�Ĵ����������
		if (pkt->stream_index == _vs->audio_stream && pkt_in_play_range) {
			_packetQueue->packet_queue_put(&_vs->audioq, pkt);
		}
		else if (pkt->stream_index == _vs->video_stream && pkt_in_play_range
			&& !(_vs->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			_packetQueue->packet_queue_put(&_vs->videoq, pkt);
		}
		else {
			av_packet_unref(pkt);
		}
	}

	void MediaState::audio_loop()
	{
		if (!_audio_frame || _audio_loop_end || !_play)
			return;

		if (_vs->abort_request)
		{
			_audio_loop_end = true;
			av_frame_free(&_audio_frame);
			return;
		}

		if (_frameQueue->frame_queue_writable(&_vs->sampq))
        {
            //av_log(NULL, AV_LOG_INFO, "**** frame_queue_writable id:%d ****\n", this->m_uniqueID);
			return;
        }
        //av_log(NULL, AV_LOG_INFO, "**** keep write id:%d ****\n", this->m_uniqueID);
		// ������Ƶ֡֡
		int got_frame = 0;
		AVRational tb;
		//int ret = 0;

		got_frame = decoder_decode_frame(&_vs->auddec, _audio_frame);
		if (got_frame == -2)
			return;
		else if (got_frame < 0)
		{
			_audio_loop_end = true;
			goto the_end;
		}
				
		if (got_frame) {
			tb = { 1, _audio_frame->sample_rate };
			// ����Ƿ�֡�����Ƿ��д�룬�������д��?��ֱ���ͷ�
			if (!(_audio_af = _frameQueue->frame_queue_peek_writable(&_vs->sampq)))
			{
				_audio_loop_end = true;
				goto the_end;
			}
			// �趨֡��pts
			_audio_af->pts = (_audio_frame->pts == AV_NOPTS_VALUE) ? NAN : _audio_frame->pts * av_q2d(tb);
			_audio_af->pos = _audio_frame->pkt_pos;
			_audio_af->serial = _vs->auddec.pkt_serial;
			_audio_af->duration = av_q2d({ _audio_frame->nb_samples, _audio_frame->sample_rate });

			/**��׼seek**/
			if (_vs->seek_flag_audio)
			{
				if (_audio_af->pts < _vs->seek_time)
				{
					//av_log(NULL, AV_LOG_WARNING, "keep audio seeking to right position\n");
					return;
				}
				else
				{
					_vs->seek_flag_audio = 0;
				}
			}
			/**��׼seek**/

			// ����������Ƶ֡ѹ���������Ƶ����?
			av_frame_move_ref(_audio_af->frame, _audio_frame);
			_frameQueue->frame_queue_push(&_vs->sampq);

		}

		if (_audio_loop_end)
		{
		the_end:
			av_frame_free(&_audio_frame);
		}
	}

	void MediaState::video_loop()
	{
		if (!_play || _video_loop_end || !_video_frame)
			return;

		if (_vs->abort_request)
		{
			_video_loop_end = true;
			goto the_end;
		}

		if (_frameQueue->frame_queue_writable(&_vs->pictq))
			return;

		double pts;
		double duration;
		int ret;

		// �����Ƶ����֡�����ʧ�ܣ���ֱ���ͷţ����û����Ƶ֡��������ȴ�
		ret = get_video_frame(_video_frame);
		if (ret == -2)
		{
			return;
		}
		if (ret < 0)
		{
			_video_loop_end = true;
			goto the_end;
		}
		if (!ret)
			return;

		duration = (_video_frame_rate.num && _video_frame_rate.den ? av_q2d({ _video_frame_rate.den, _video_frame_rate.num }) : 0);
		// ����֡��pts��duration��
		pts = (_video_frame->pts == AV_NOPTS_VALUE) ? NAN : _video_frame->pts * av_q2d(_tb);

		/**��׼seek**/
		if (_vs->seek_flag_video)
		{
            int64_t playTime = pts + (_skip_to_last ? 0.5 : 0.0);//防止ffmpeg解码返回AVERROR_EOF
			if (playTime < _vs->seek_time)
			{
				//av_log(NULL, AV_LOG_WARNING, "keep video seeking to right position\n");
				return;
			}
			else
			{
                _skip_to_last = false;
				_vs->seek_flag_video = 0;
			}
		}
		/**��׼seek**/

		// ���뵽�ѽ������?
		ret = queue_picture(_video_frame, pts, duration, _video_frame->pkt_pos, _vs->viddec.pkt_serial);
		av_frame_unref(_video_frame);

		if (ret < 0)
			_video_loop_end = true;
		else if (!_getFirstPicture)
		{
			if (_vs->paused)
			{
				_vs->force_refresh = 1;
			}
		}
	
		if (_video_loop_end)
		{
		the_end:
			av_frame_free(&_video_frame);
		}
	}

    int MediaState::audio_decode_frame()
    {
        if (!this->_vs)
        {
            return -2;
        }
        int data_size, resampled_data_size;
        int64_t dec_channel_layout;
        av_unused double audio_clock0;
        int wanted_nb_samples;
        Frame *af;

        if (_vs->paused)
        {
            return -1;
        }

        do {
#if defined(_WIN32)
            while (_frameQueue->frame_queue_nb_remaining(&_vs->sampq) == 0) {
                if ((av_gettime_relative() - _audio_callback_time) > 1000000LL * _vs->audio_hw_buf_size / _vs->audio_tgt.bytes_per_sec / 2)
                    return -1;
                av_usleep(1000);
            }
#endif
            //av_log(NULL, AV_LOG_INFO, "**** call frameQueue readable id:%d ****\n", this->m_uniqueID);
            if (!(af = _frameQueue->frame_queue_peek_readable(&_vs->sampq)))
                return -1;
            _frameQueue->frame_queue_next(&_vs->sampq);
        } while (af->serial != _vs->audioq.serial);
        //av_log(NULL, AV_LOG_INFO, "**** keep audio going id:%d ****\n", this->m_uniqueID);
        data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
            af->frame->nb_samples,
            (AVSampleFormat)af->frame->format, 1);
        dec_channel_layout =
            (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
            af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
        wanted_nb_samples = synchronize_audio(af->frame->nb_samples);
        if (af->frame->format != _vs->audio_src.fmt ||
            dec_channel_layout != _vs->audio_src.channel_layout ||
            af->frame->sample_rate != _vs->audio_src.freq ||
            (wanted_nb_samples != af->frame->nb_samples && !_vs->swr_ctx)) {
            swr_free(&_vs->swr_ctx);
            _vs->swr_ctx = swr_alloc_set_opts(NULL,
                _vs->audio_tgt.channel_layout, _vs->audio_tgt.fmt, _vs->audio_tgt.freq,
                dec_channel_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
                0, NULL);
            if (!_vs->swr_ctx || swr_init(_vs->swr_ctx) < 0) {
                av_log(NULL, AV_LOG_ERROR,
                    "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                    _vs->audio_tgt.freq, av_get_sample_fmt_name(_vs->audio_tgt.fmt), _vs->audio_tgt.channels);
                swr_free(&_vs->swr_ctx);
                return -1;
            }
            _vs->audio_src.channel_layout = dec_channel_layout;
            _vs->audio_src.channels = af->frame->channels;
            _vs->audio_src.freq = af->frame->sample_rate;
            _vs->audio_src.fmt = (AVSampleFormat)af->frame->format;
        }

        if (_vs->swr_ctx) {
            const uint8_t **in = (const uint8_t **)af->frame->extended_data;
            uint8_t **out = &_vs->audio_buf1;
            int out_count = (int64_t)wanted_nb_samples * _vs->audio_tgt.freq / af->frame->sample_rate + 256;
            int out_size = av_samples_get_buffer_size(NULL, _vs->audio_tgt.channels, out_count, _vs->audio_tgt.fmt, 0);
            int len2;
            if (out_size < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
                return -1;
            }
            if (wanted_nb_samples != af->frame->nb_samples) {
                if (swr_set_compensation(_vs->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * _vs->audio_tgt.freq / af->frame->sample_rate,
                    wanted_nb_samples * _vs->audio_tgt.freq / af->frame->sample_rate) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                    return -1;
                }
            }
            av_fast_malloc(&_vs->audio_buf1, &_vs->audio_buf1_size, out_size);
            if (!_vs->audio_buf1)
                return AVERROR(ENOMEM);
            len2 = swr_convert(_vs->swr_ctx, out, out_count, in, af->frame->nb_samples);
            if (len2 < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
                return -1;
            }
            if (len2 == out_count) {
                av_log(NULL, AV_LOG_WARNING, "audio buffer _vs probably too small\n");
                if (swr_init(_vs->swr_ctx) < 0)
                    swr_free(&_vs->swr_ctx);
            }
            _vs->audio_buf = _vs->audio_buf1;
            resampled_data_size = len2 * _vs->audio_tgt.channels * av_get_bytes_per_sample(_vs->audio_tgt.fmt);
        }
        else {
            _vs->audio_buf = af->frame->data[0];
            resampled_data_size = data_size;
        }
        audio_clock0 = _vs->audio_clock;
        /* update the audio clock with the pts */
        if (!isnan(af->pts))
            _vs->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
        else
            _vs->audio_clock = NAN;
        _vs->audio_clock_serial = af->serial;
#ifdef DEBUG
        {
            static double last_clock;
//            printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
//                _vs->audio_clock - last_clock,
//                _vs->audio_clock, audio_clock0);
            last_clock = _vs->audio_clock;
        }
#endif
        return resampled_data_size;
    }

    int MediaState::sdl_audio_callback(Uint8 *stream, int len, bool clean)
    {
		_main_mutex.lock();
        if (!_inited || _exit || _event == FF_OVER || !this->_vs /*|| _event == FF_PAUSE*/) {
			_main_mutex.unlock();
            return -1;
        }
        
        int audio_size, len1;

        this->_audio_callback_time = av_gettime_relative();

        while (len > 0) {
            if (_vs->audio_buf_index >= _vs->audio_buf_size) {
                audio_size = this->audio_decode_frame();
                //av_log(NULL, AV_LOG_WARNING, "audio_size, %d\n",audio_size);
                if (-2 == audio_size)
                {
                    _main_mutex.unlock();
                    return -1;
                }
                if (audio_size < 0) {
                    /* if error, just output silence */
                    //av_log(NULL, AV_LOG_WARNING, "error audio_size < 0, %d\n",audio_size);
                    _vs->audio_buf = NULL;
                    _vs->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / _vs->audio_tgt.frame_size * _vs->audio_tgt.frame_size;
                }
                else {
                    _vs->audio_buf_size = audio_size;
                }
                _vs->audio_buf_index = 0;
            }
            len1 = _vs->audio_buf_size - _vs->audio_buf_index;
            if (len1 > len)
                len1 = len;
            //if (!_vs->muted && _vs->audio_buf && _vs->audio_volume == SDL_MIX_MAXVOLUME)
            //    memcpy(stream, (uint8_t *)_vs->audio_buf + _vs->audio_buf_index, len1);
            //    //SDL_memcpy(stream, (uint8_t *)_vs->audio_buf + _vs->audio_buf_index, len1);
            //else {
            //    memset(stream, 0, len1); //����,��ͣ
            //    //SDL_memset(stream, 0, len1);
            //    if (!_vs->muted && _vs->audio_buf)
            //        SDL_MixAudioFormat(stream, (uint8_t *)_vs->audio_buf + _vs->audio_buf_index, AUDIO_S16SYS, len1, _vs->audio_volume);
            //}
            if (clean)
            {
                memset(stream, 0, len1);
            }
            if (!_muted && _vs->audio_buf && _play){
                SDL_MixAudioFormat(stream, (uint8_t *)_vs->audio_buf + _vs->audio_buf_index, AUDIO_S16SYS, len1, _vs->audio_volume);
                //av_log(NULL, AV_LOG_WARNING, "SDL_MixAudioFormat, %d\n",this->m_uniqueID);
            }
            //av_log(NULL, AV_LOG_WARNING, "SDL_MixAudioFormat id:%d len:%d len1:%d audio_buf_index:%d audio_buf_size:%d\n", this->m_uniqueID, len, len1, _vs->audio_buf_index, _vs->audio_buf_size);
            len -= len1;
            stream += len1;
            _vs->audio_buf_index += len1;
        }
        _vs->audio_write_buf_size = _vs->audio_buf_size - _vs->audio_buf_index;
        /* Let's assume the audio driver that _vs used by SDL has two periods. */
        if (!isnan(_vs->audio_clock)) {
            this->set_clock_at(&_vs->audclk, _vs->audio_clock - (double)(2 * _vs->audio_hw_buf_size + _vs->audio_write_buf_size) / _vs->audio_tgt.bytes_per_sec, _vs->audio_clock_serial, this->_audio_callback_time / 1000000.0);
            this->sync_clock_to_slave(&_vs->extclk, &_vs->audclk);
        }
        _main_mutex.unlock();
        return _muted ? 0 : 1;
    }
}



