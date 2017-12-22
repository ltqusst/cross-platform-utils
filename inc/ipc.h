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

class ipc_connection;
class ipc_io_service;

#include "utils.h"

class ipc_io_service: public NonCopyable
{
public:
	typedef std::shared_ptr<ipc_io_service> Ptr;

	static ipc_io_service::Ptr create(const char* type = "");

	virtual ~ipc_io_service() {};
	virtual void run() = 0;
	virtual void stop() = 0;
	virtual void associate(ipc_connection * pconn) = 0;
	virtual void unassociate(ipc_connection * pconn) = 0;
	virtual OS_HANDLE native_handle(void) = 0;
protected:
	ipc_io_service() {};
};


class ipc_connection : public NonCopyable
{
public:
	typedef std::shared_ptr<ipc_connection> Ptr;

	static ipc_connection::Ptr create(ipc_io_service * p_service, const char* ipc_type, const char* server_name = "");
	
	virtual ~ipc_connection() {}
	virtual OS_HANDLE native_handle() = 0;
	virtual ipc_io_service & get_service() { return m_service; }

	//on Windows, notify means one IO request is completed or error occured
	//on Linux, notify means one IO request type is ready to issue without blocking
	virtual void notify(int error_code, int transferred_cnt, uintptr_t hint) = 0;

	//blocking/sync version
	//  based on async version, so they cannot be called within async handler!
	//  or deadlock may occur. actually no blocking operation should be called inside
	//  async handler at all ( or async mode will have performance issue ).
	//
	//  so inside async handler, user can:
	//		1. response by using async_write
	//      2. dispatch the response task to another thread, and in that thread, 
	//         sync version IO can be used without blocking io_service.
	virtual void read(void * pbuff, const int len, int *lp_cnt = NULL) = 0;
	virtual void write(void * pbuff, const int len) = 0;

	//can be used on duplicate fd(file descriptor) on Linux or File Handle on Windows
	virtual void read(OS_HANDLE &oshd) = 0;
	virtual void write(OS_HANDLE oshd) = 0;

	//client(blocking is acceptable because usually its short latency)
	virtual int connect(const std::string & serverName) = 0;
	virtual int listen(void) = 0;
	virtual void close(void) = 0; //close connection

	//async callbacks are free for register by simple assign
	std::function<void(ipc_connection * pconn, const std::error_code & ec, std::size_t len)>	on_read;
	std::function<void(ipc_connection * pconn)>													on_accept;
	std::function<void(ipc_connection * pconn)>													on_close;
protected:
	ipc_connection(ipc_io_service & s) :m_service(s) {}
	ipc_io_service &    	m_service;
};

#endif
