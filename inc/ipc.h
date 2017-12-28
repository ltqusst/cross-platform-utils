#ifndef _IPC_H_
#define _IPC_H_

#ifdef WIN32
#include "windows.h"
#endif

#ifdef linux
#include <unistd.h>
#endif

#ifdef WIN32
typedef HANDLE OS_HANDLE;
#define INVALID_OS_HANDLE INVALID_HANDLE_VALUE
#define CLOSE_OS_HANDLE(a) CloseHandle(a)
#else
typedef int OS_HANDLE;
#define INVALID_OS_HANDLE -1
#define CLOSE_OS_HANDLE(a) ::close(a)
#endif

//
// service:   run in its own thread (so all callbacks also called from there, make sure do not blocking inside call back)
//
// connection:  with the help of service coupled together
//			  this helper fulfills common async IO(include passive connection) requests
//
// one service can co-work with many different kind of connections,
// for example, socket and namedPipe can be implemented in 2 different executor
//
// they are not value-type but identity-type, so pure-virtual class is a way to hide impletation better than pimpl
//    https://stackoverflow.com/questions/825018/pimpl-idiom-vs-pure-virtual-class-interface
#include "utils.h"


namespace cross
{

// we have to allow dangling wrapper because no exception is allowed here
// so we do not use RAll, the class is much more like OS handle which is
// allowed to be in dangling/zombie state, and call member function on
// zombie object will trigger an error as the downside.
//
// even so, wrapper still have chance to release the resource on self-destruction
// to allow resource free automatically as the upside.(so we can return freely)


class ipc_connection;
class ipc_connection_poller;

template<class KEY>
class fast_mapper
{
	static const int					m_map_fast_size = 1024;
	std::mutex							m_mutex;
	ipc_connection *					m_map_fast[m_map_fast_size] = {0};
	std::map<KEY, ipc_connection*>		m_map;
public:
	ipc_connection* & get(KEY oshd){
		std::unique_lock<std::mutex> lk(m_mutex);
		auto v = key2int(oshd, std::is_pointer<KEY>());
		if (v >= 0 && v < m_map_fast_size)
			return m_map_fast[v];

		if (m_map.find(oshd) == m_map.end())
			m_map.insert(std::make_pair(oshd, (ipc_connection*)NULL));

		return m_map[oshd];
	}
	void erase(KEY oshd){
		std::unique_lock<std::mutex> lk(m_mutex);
		auto v = key2int(oshd, std::is_pointer<KEY>());
		if (v >= 0 && v < m_map_fast_size) {
			m_map_fast[v] = NULL;
			return;
		}
		auto it = m_map.find(oshd);
		if (it != m_map.end())
			m_map.erase(it);
	}

	int key2int(KEY k, std::false_type) {
		//not pointer
		return static_cast<int>(k);
	}
	int key2int(KEY k, std::true_type) {
		//pointer type 
		return static_cast<int>(reinterpret_cast<uintptr_t>(k));
	}
};

struct ipc_poll_event
{
	enum class event{NONE=0, POLLIN, POLLHUP} 	e = event::NONE;
	ipc_connection	*						pconn = nullptr;
};

class ipc_connection : public NonCopyable
{
public:
	virtual ~ipc_connection(){};
	virtual OS_HANDLE native_handle() = 0;

	//blocking/sync version
	//  based on async version, so they cannot be called within async handler!
	//  or deadlock may occur. actually no blocking operation should be called inside
	//  async handler at all ( or async mode will have performance issue ).
	//
	//  so inside async handler, user can:
	//		1. response by using async_write
	//      2. dispatch the response task to another thread, and in that thread, 
	//         sync version IO can be used without blocking io_service.
	virtual EResult read(void * pbuff, const int len, int *lp_cnt = NULL) = 0;
	virtual EResult write(void * pbuff, const int len) = 0;

	//can be used on duplicate fd(file descriptor) on Linux or File Handle on Windows
	virtual void read(OS_HANDLE &oshd) = 0;
	virtual void write(OS_HANDLE oshd) = 0;

	//the real creation of the underlying resource
	virtual EResult connect(const std::string & serverName) = 0;
	virtual EResult listen(const std::string & serverName) = 0;
	virtual EResult accept(ipc_connection * listener) = 0;

	//close underlying resource
	virtual void close(void) = 0;

	friend class ipc_connection_poller;

protected:

	//on Windows, notify means one IO request is completed or error occured
	//on Linux, notify means one IO request type is ready to issue without blocking
	virtual void notify(int error_code, int transferred_cnt, uintptr_t hint, ipc_poll_event * pevt) = 0;

	ipc_connection(ipc_connection_poller * p):m_poller(p){};
	ipc_connection_poller *    	m_poller;
};



class ipc_connection_poller: public NonCopyable
{
	//a general mapping facility
	fast_mapper<OS_HANDLE>	m_mapper;

	static const int        		m_epollTimeout = 100;


	std::queue < std::function<void()> > m_prewait_callbacks;

#ifdef WIN32
	OS_HANDLE						m_h_io_compl_port;
#else
	//Linux
	static const int 				m_epollSize = 1000;
	int								m_epollFd;
#endif

public:
	ipc_connection_poller();
	~ipc_connection_poller();
	void add(ipc_connection * pconn);
	void del(ipc_connection * pconn);
	EResult wait(ipc_poll_event * pevt);

	
	//queue prewait callbacks helper function 
	// make sure the callback do not throw exception or return error code
	template<typename _Callable, typename... _Args>
	void queue_prewait_callback(_Callable&& __f, _Args&&... __args){
		std::function<void()> func = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);
		m_prewait_callbacks.push(func);
	}

	OS_HANDLE native_handle(void);
};




#ifdef linux
//Unix Domain Sockets
class ipc_connection_linux_UDS : public ipc_connection
{
public:
	//bare minimum constructor
	ipc_connection_linux_UDS(ipc_connection_poller * p);
	~ipc_connection_linux_UDS();

	virtual void notify(int error_code, int transferred_cnt, uintptr_t hint, ipc_poll_event * pevt);

	virtual OS_HANDLE native_handle() { return m_fd; }

	//blocking/sync version(based on async version)
	virtual EResult read(void * pbuff, const int len, int *lp_cnt = NULL);
	virtual EResult write(void * pbuff, const int len);

	virtual void read(OS_HANDLE &oshd);
	virtual void write(OS_HANDLE oshd);

	//client(blocking is acceptable because usually its short latency)
	virtual void close(void);

	//the real creation of the underlying resource
	virtual EResult connect(const std::string & serverName);
	virtual EResult listen(const std::string & serverName);
	virtual EResult accept(ipc_connection * listener);

protected:
	bool set_block_mode(bool makeBlocking = true);

	constexpr static const char * CLI_PATH = "/var/tmp/";
	constexpr static const int  m_numListen = 5;

	int 					m_fd;
	const char *			m_name;		//IPC name

	enum class state {EMPTY=0, LISTEN, CONN, DISCONN}	m_state;
};
#endif

#ifdef WIN32
class ipc_connection_win_namedpipe : public ipc_connection
{
public:
	ipc_connection_win_namedpipe(ipc_connection_poller * p);
	~ipc_connection_win_namedpipe();
	virtual void notify(int error_code, int transferred_cnt, uintptr_t hint, ipc_poll_event * pevt);

	virtual OS_HANDLE native_handle() { return m_oshd; }

	//blocking/sync version(based on async version)
	virtual EResult read(void * pbuff, const int len, int *lp_cnt = NULL);
	virtual EResult write(void * pbuff, const int len);

	//can be used on duplicate fd(file descriptor) on Linux or File Handle on Windows
	virtual void read(OS_HANDLE &oshd);
	virtual void write(OS_HANDLE oshd);

	virtual EResult connect(const std::string & serverName);
	virtual EResult listen(const std::string & serverName);
	virtual EResult accept(ipc_connection * listener);

	virtual void close(void);

protected:

private:

	friend class ipc_connection_poller;

	EResult wait_for_connection(void);
	void trigger_async_cache_read(void);

	HANDLE					m_oshd;

	// <0 means no cached byte0
	// >0 means 1 byte is cached and should be filled to user's buffer first
	unsigned char			m_cache_byte0;
	bool					m_cache_empty;
	OVERLAPPED				m_cache_overlapped;
	OVERLAPPED				m_error_overlapped;
	OVERLAPPED				m_waitconn_overlapped;
	OVERLAPPED				m_sync_overlapped;

	std::string				m_name;		//IPC name
	enum class state { EMPTY = 0, LISTEN, CONN, DISCONN }	m_state;

	friend std::ostream& operator<<(std::ostream& s, const ipc_connection_win_namedpipe::state & d);
};
#endif


}

#endif
