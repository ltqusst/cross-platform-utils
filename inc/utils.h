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
	NonCopyable(const NonCopyable &) {};
	NonCopyable & operator= (const NonCopyable &) {};
};

//====================================================================
//C double trick to generate constant string symbol of line number
//https://stackoverflow.com/questions/2751870/how-exactly-does-the-double-stringize-trick-work
#define S(x) #x						//"42"
#define S_(x) S(x)					//S(42)
#define __STR_LINE__ S_(__LINE__)	//S_(__LINE__)

//====================================================================
//exception
#include <stdexcept>
#define THROW_SYSTEM_ERROR(msg, code) \
	throw std::system_error((code),std::system_category(), std::string(msg) + " (at) " __FILE__ ":"  __STR_LINE__);\

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
#include <exception>
template<class T>
class Expected
{
	union {
		T ham;
		std::exception_ptr spam;
	};
	bool gotHam;
	Expected() {} //used internally (construct using static factory method)

public:
	//implicit constructor from value of T
	Expected(const T& rhs) : ham(rhs), gotHam(true) {}
	Expected(T&& rhs): ham(std::move(rhs)), gotHam(true) {}

	//copy
	Expected(const Expected & rhs) : gotHam(rhs.gotHam) {
		if (gotHam) new (&ham) T(rhs.ham);
		else new (&spam) std::exception_ptr(rhs.spam);
	}

	Expected(Expected && rhs) : gotHam(rhs.gotHam) {
		if (gotHam) new (&ham) T(std::move(rhs.ham));
		else new (&spam) std::exception_ptr(std::move(rhs.spam));
	}

	~Expected() {
		using std::exception_ptr;
		if (gotHam) ham.~T();
		else spam.~exception_ptr();
	}

	void swap(Expected& rhs) {
		if (gotHam) {
			if (rhs.gotHam) {
				using std::swap;
				swap(ham, rhs.ham);
			} else {
				auto t = std::move(rhs.spam);
				new(&rhs.ham) T(std::move(ham));
				new(&spam) std::exception_ptr(t);
				std::swap(gotHam, rhs.gotHam);
			}
		} else {
			if (rhs.gotHam) {
				rhs.swap(*this);
			}else {
				spam.swap(rhs.spam);
				std::swap(gotHam, rhs.gotHam);
			}
		}
	}

	//builder
	template<class E>
	static Expected<T> fromException(const E& exception) {
		if (typeid(exception) != typeid(E)) {
			throw std::invalid_argument("slicing detected");
		}
		return fromException(std::make_exception_ptr(exception));
	}

	static Expected<T> fromException(std::exception_ptr p) {
		Expected<T> result;
		result.gotHam = false;
		new(&result.spam) std::exception_ptr(std::move(p));
		return result;
	}

	static Expected<T> fromException() {
		return fromException(std::current_exception());
	}

	bool valid() const {
		return gotHam;
	}

	T& get() {
		if (!gotHam) std::rethrow_exception(spam);
		return ham;
	}

	const T& get() const {
		if (!gotHam) std::rethrow_exception(spam);
		return ham;
	}


	template<class E>
	bool hasException() const {
		try {
			if (!gotHam) std::rethrow_exception(spam);
		}catch (const E& object) {
			return true;
		}
		catch (...) {
		}
		return false;
	}

	//to support:
	//   auto r = Expected<string>::fromCode([]{ ... });
	template<class F>
	static Expected fromCode(F fun) {
		try {
			return Expected(fun());
		}
		catch (...) {
			return fromException();
		}
	}

};

#endif
