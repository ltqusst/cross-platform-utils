#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef WIN32
#include "windows.h"
#endif

#ifdef linux
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//net
#include <net/if_arp.h>
#include <net/if.h>
//sys
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h> /* epoll function */
#include <sys/un.h>
#endif

//C++
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <cassert>
#include <cstddef>
#include <string>
#include <functional>
#include <stdexcept>
#include <map>
#include <tuple>
#include <deque>
#include <queue>
#include <atomic>
#include <iostream>
#include <thread>
#include <cassert>
#include <system_error>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <sstream>
#include <iterator>

namespace cross
{

//====================================================================
// a safe replacement for strcpy
// let compiler deduce destsz whenever possible
template<size_t destsz>
size_t string_copy(char(&dest)[destsz], const std::string & str, size_t pos = 0)
{
#ifdef WIN32
	strcpy_s(dest, destsz, str.c_str() + pos);
	return std::min<>(destsz - 1, str.length() - pos);
#else
	size_t cnt = str.copy(dest, destsz - 1, pos);
	cnt = std::min<size_t>(cnt, destsz - 1);
	dest[cnt] = '\0';
	return cnt;
#endif
}

//====================================================================
class NonCopyable
{
protected:
	NonCopyable() {}
	~NonCopyable() {} /// Protected non-virtual destructor
private:
	NonCopyable(const NonCopyable &) {}
	NonCopyable & operator= (const NonCopyable &) {return *this;}
};

//====================================================================
//C double trick to generate constant string symbol of line number
//https://stackoverflow.com/questions/2751870/how-exactly-does-the-double-stringize-trick-work
#define S(x) #x						//"42"
#define S_(x) S(x)					//S(42)
#define __STR_LINE__ S_(__LINE__)	//S_(__LINE__)

#define DBG_LINENO " at " __FILE__ ":"  __STR_LINE__
//====================================================================
//exception
#include <stdexcept>
#define THROW_SYSTEM_ERROR(msg, code) \
	throw std::system_error((code),std::system_category(), std::string(msg) + " (at) " __FILE__ ":"  __STR_LINE__);\

#define THROW_(exception_type, msg) \
	throw exception_type(std::string(msg) + " (at) " __FILE__ ":"  __STR_LINE__);\

//====================================================================
//assert
#define assert_line(cond)	\
	assert(cond || (fprintf(stderr, #cond " assert failed (at) %s:%d", __FILE__, __LINE__), 0));


//====================================================================
// Andrei Alexandrescu's talk (Systematic Error Handling in C++)
// https://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Andrei-Alexandrescu-Systematic-Error-Handling-in-C
// ScopeGuard
template<class Fun>
class ScopeGuard {
	Fun m_f;
	bool m_active;
public:
	ScopeGuard(Fun f) :m_f(std::move(f)), m_active(true) {}
	~ScopeGuard() { if (m_active) m_f(); }
	void dismiss() { m_active = false; }

	ScopeGuard() = delete;
	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard & operator=(const ScopeGuard&) = delete;
	ScopeGuard(ScopeGuard && rhs) :
		m_f(std::move(rhs.m_f)),
		m_active(rhs.m_active) {
		rhs.dismiss();
	}
};

//for function template support type deduction while class template doesn't
template <class Fun>
ScopeGuard<Fun> make_scopeGuard(Fun f) {
	return ScopeGuard<Fun>(std::move(f));
}

//====================================================================
// Andrei Alexandrescu's talk (Systematic Error Handling in C++) 
//Expected<T> idiom
// since we do not use exceptions we did some modifications
//
// EResult is a common value type that similar to a int
// except:
//	1. when error occurs, log information is allowed to be appended using C++ stream syntax
//  2. when no one checks the occurred error(by implicitly convert to int), it will terminate
//     program and display error message (which is good for debug, can be skipped).
// note, only negative value is considered to be an error.

#include <exception>

#if 1
#define EResult_DEBUG(msg)
#else
#define EResult_DEBUG(msg) \
	std::cout << msg << " " << "[" << this << "] m_pdetail=" << m_pdetail \
	<< " err=" << (m_pdetail?m_pdetail->m_err:0) \
	<< " cnt=" << (m_pdetail?m_pdetail->m_count:0)	<< std::endl;
#endif

class EResult
{
	//reference counting based detail information
	struct Detail
	{
		bool 				m_checked  	= false;
		int 				m_err	 	= 0;
		int 				m_err_sys 	= 0;
		std::stringstream   m_ss{};
		size_t   			m_count 	= 0;
	};

	//no detail by default to maximize normal execution flow
	Detail					* m_pdetail = NULL;

	void deref()
	{
		if(m_pdetail){
			m_pdetail->m_count--;

			EResult_DEBUG("**** deref");

			if(m_pdetail->m_count == 0){
				//only raise unchecked error on last referencing
				//we took unix tradition, error>=0 is allowed as normal return value
				if(m_pdetail->m_err < 0 && !m_pdetail->m_checked){
					std::cerr << "unchecked error code: "
							<< std::endl << "\t"
							<< message()
							<< std::endl;
					delete m_pdetail;
					std::terminate();
				}
				else{
					EResult_DEBUG("**** DELETE m_pdetail")
					delete m_pdetail;
				}

				m_pdetail = NULL;
			}
		}
	}

public:

	//ctor
	EResult(int err = 0, int err_sys = 0){
		//m_pdetail == NULL means no error
		//and little expensive when error occurs
		if(err || err_sys){
			m_pdetail = new Detail;
			m_pdetail->m_err = err;
			m_pdetail->m_err_sys = err_sys;
			m_pdetail->m_count = 1;
		}
		EResult_DEBUG("**** ctor");
	}

	//copy ctor
	EResult(const EResult & rhs):
		m_pdetail(rhs.m_pdetail){
		EResult_DEBUG("**** copy ctor");
		if(m_pdetail)
			m_pdetail->m_count++;
	}

	//copy assign
	EResult& operator=(const EResult & rhs){
		EResult_DEBUG("**** copy assign");
		deref();
		m_pdetail = rhs.m_pdetail;
		if(m_pdetail)
			m_pdetail->m_count++;
		return *this;
	}

	//move ctor: just empty rhs
	EResult(EResult && rhs) :
		m_pdetail(rhs.m_pdetail){
		EResult_DEBUG("**** move ctor ");
		rhs.m_pdetail = NULL;
	}

	//move assign: just empty rhs
	EResult& operator=(EResult && rhs){
		EResult_DEBUG("**** move assign");
		deref();
		m_pdetail = rhs.m_pdetail;
		rhs.m_pdetail = NULL;
		return *this;
	}

	~EResult() {
		deref();
	}

	std::string message(void)
	{
		//we took unix tradition, error>=0 is allowed as normal return value
		if(m_pdetail && m_pdetail->m_err < 0){
			std::stringstream ss;
			ss << "error (" << m_pdetail->m_err << "): " << m_pdetail->m_ss.str();
			//err_sys is only meaningful when err occurs
			if(m_pdetail->m_err_sys){
				std::error_code ec(m_pdetail->m_err_sys, std::system_category());
				ss << ec.message() << " (" << m_pdetail->m_err_sys << ")";
			}
			return ss.str();
		}

		return "no error";
	}

	operator int()
	{
		EResult_DEBUG("**** operator int")
		if(m_pdetail){
			m_pdetail->m_checked = true;
			return m_pdetail->m_err;
		}
		return 0;
	}

	int error_sys(){
		if(m_pdetail){
			m_pdetail->m_checked = true;
			return m_pdetail->m_err_sys;
		}
		return 0;	//no error
	}

	//ignore error some time
	void ignore(){
		if(m_pdetail)
			m_pdetail->m_checked = true;
	}

	//type-safe syntax for appending any additional error message
	template<class T>
	EResult& operator<<(const T& t)
	{
		EResult_DEBUG("**** operator<<");

		//only log error message on error construct
		if(m_pdetail)
			m_pdetail->m_ss << t;
		return *this;
	}

};

#define anERROR(...) cross::EResult(__VA_ARGS__) <<__FILE__<<":"<<__LINE__ << "\t"



//====================================================
static int getPID()
{
#ifdef linux
	return getpid();
#endif

#ifdef WIN32
	return ::GetCurrentProcessId();
#endif
}

static EResult daemonize(void)
{
#ifdef linux
	int nochdir = 1;
	int noclose = 1;
	if(daemon(nochdir, noclose) < 0)
	{
		return anERROR(-1, errno) << "daemon() failed";
	}
#endif

#ifdef WIN32
#endif

	return 0;
}


}
#endif
