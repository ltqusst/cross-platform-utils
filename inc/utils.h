#ifndef _UTILS_H_
#define _UTILS_H_

//====================================================================
// a safe replacement for strcpy
// let compiler deduce destsz whenever possible
template<size_t destsz>
int string_copy(char(&dest)[destsz], const std::string & str, size_t pos = 0)
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
//ScopeGuard
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
ScopeGuard<Fun> scopeGuard(Fun f) {
	return ScopeGuard<Fun>(std::move(f));
}

//====================================================================
// Andrei Alexandrescu's talk (Systematic Error Handling in C++) 
//Expected<T> idiom
// since we do not use exceptions we did some modifications
//
// EResult is a common value type that similar to a int
// except that when error occurs, lot of
#include <exception>

#if 1
#define EResult_DEBUG(msg)
#else
#define EResult_DEBUG(msg) \
	std::cout << msg << " " << "[" << this << "] m_pdetail=" << m_pdetail \
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
				if(m_pdetail->m_err && !m_pdetail->m_checked){
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
		EResult_DEBUG("**** ctor");

		//m_pdetail == NULL means no error
		//and little expensive when error occurs
		if(err || err_sys){
			m_pdetail = new Detail;
			m_pdetail->m_err = err;
			m_pdetail->m_err_sys = err_sys;
			m_pdetail->m_count = 1;
		}
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
	}

	~EResult() {
		deref();
	}

	std::string message(void)
	{
		if(m_pdetail && m_pdetail->m_err){
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

	int error(){
		if(m_pdetail){
			m_pdetail->m_checked = true;
			return m_pdetail->m_err;
		}
		return 0;	//no error
	}

	int error_sys(){
		if(m_pdetail){
			m_pdetail->m_checked = true;
			return m_pdetail->m_err_sys;
		}
		return 0;	//no error
	}

	//type-safe syntax for appending any additional error message
	template<class T>
	EResult& operator<<(const T& t)
	{
		//only log error message on error construct
		if(m_pdetail)
			m_pdetail->m_ss << t;
		return *this;
	}

};

#define anERROR(...) EResult(__VA_ARGS__) <<__FILE__<<":"<<__LINE__ << "\t"


#endif
