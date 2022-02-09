//
//  Utils.hpp
//
//  Created by AndyZheng on 2018/3/2.
//

#ifndef FFmpeg_Utils_hpp
#define FFmpeg_Utils_hpp

#include <stdio.h>
#include <mutex>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include <iostream>

namespace FFMPEG{

class Uncopyable{
protected:
    Uncopyable(){};
    virtual ~Uncopyable(){};
    
private:
    Uncopyable(const Uncopyable&);
    Uncopyable& operator=(const Uncopyable&);
};

class TTimer : private Uncopyable{
public:
    explicit TTimer(const std::string title, bool printMsg = true)
    {
#ifdef DEBUG
        struct timeval current;
        gettimeofday(&current, NULL);
        _cur_time = current.tv_usec/1000.0f;
        _title = title;
        _printMsg = printMsg;
#endif
    }
    ~TTimer()
    {
#ifdef DEBUG
        struct timeval current;
        gettimeofday(&current, NULL);
        double _time_used = current.tv_usec/1000.0f - _cur_time;
        if (_printMsg) {
            std::cout << _title;
            std::cout << " : ";
            std::cout << _time_used;
            std::cout << "ms" <<std::endl;
        }
        ++s_count;
        s_total_time_use += _time_used;
#endif
    }
    
    static void average(const std::string title)
    {
#ifdef DEBUG
        if (0 == s_count) {
            return;
        }
        double t = s_total_time_use / (double)s_count;
        std::cout << title;
        std::cout << " : ";
        std::cout << t;
        std::cout << "ms" <<std::endl;
        s_total_time_use = 0;
        s_count = 0;
#endif
    }
    
#ifdef _WIN32
	int gettimeofday(struct timeval * val, struct timezone *)
	{
		if (val)
		{
			LARGE_INTEGER liTime, liFreq;
			QueryPerformanceFrequency(&liFreq);
			QueryPerformanceCounter(&liTime);
			val->tv_sec = (long)(liTime.QuadPart / liFreq.QuadPart);
			val->tv_usec = (long)(liTime.QuadPart * 1000000.0 / liFreq.QuadPart - val->tv_sec * 1000000.0);
		}
		return 0;
	}
#endif
private:
    static int s_count;
    static double s_total_time_use;
    double _cur_time;
    std::string _title;
    bool _printMsg;
};

int TTimer::s_count = 0;
double TTimer::s_total_time_use = 0;

}
#endif /* Utils_hpp */
