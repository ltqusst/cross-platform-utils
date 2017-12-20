
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
#include <memory>
#include "ipc.h"


//==========================================================================================================================
//service imeplmentation

class ipc_io_service_impl : public ipc_io_service
{
public:
	ipc_io_service_impl();
	~ipc_io_service_impl();
	virtual void run();
	virtual void stop();
	virtual void associate(ipc_connection * pconn) ;
	virtual void unassociate(ipc_connection * pconn);
	virtual OS_HANDLE native_handle(void);

private:
	static const int        m_epollTimeout = 100;
	std::atomic<bool>		m_exit;

	//a general mapping facility
	static const int							m_map_fast_size = 1024;
	ipc_connection *							m_map_fast[m_map_fast_size];
	std::map<OS_HANDLE, ipc_connection*>		m_map;

	ipc_connection* & get(OS_HANDLE oshd)
	{
		unsigned long v = (unsigned long)oshd;
		if (v >= 0 && v < m_map_fast_size)
			return m_map_fast[v];

		if (m_map.find(oshd) == m_map.end())
			m_map.insert(std::make_pair(oshd, (ipc_connection*)NULL));

		return m_map[oshd];
	}
	void erase(OS_HANDLE oshd)
	{
		unsigned long v = (unsigned long)oshd;
		if (v >= 0 && v < m_map_fast_size) {
			m_map_fast[v] = NULL;
			return;
		}
		auto it = m_map.find(oshd);
		if (it != m_map.end())
			m_map.erase(it);
	}

#ifdef WIN32
	OS_HANDLE						m_h_io_compl_port;
#else
	//Linux
	static const int                m_maxEpollEvents = 100;
	static const int 				m_epollSize = 1000;
	int								m_epollFd;
#endif
};

ipc_io_service::Ptr ipc_io_service::create(const char* type)
{
	return ipc_io_service::Ptr(new ipc_io_service_impl());
}

ipc_io_service_impl::ipc_io_service_impl() :
	m_map_fast{}
{
#ifdef WIN32
	m_h_io_compl_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (m_h_io_compl_port == NULL) 
		THROW_SYSTEM_ERROR("ipc_io_service ctor: CreateIoCompletionPort failed.", GetLastError());
#else
	m_epollFd = epoll_create(m_epollSize);
	if (m_epollFd < 0)
		THROW_SYSTEM_ERROR("ipc_io_service ctor: epoll_create failed.", errno);
#endif
}

ipc_io_service_impl::~ipc_io_service_impl()
{
	stop();
#ifdef WIN32
	CloseHandle(m_h_io_compl_port);
#else
	close(m_epollFd);
#endif
}

void ipc_io_service_impl::stop()
{
#ifdef WIN32
	m_exit.store(true);
#else
	m_exit.store(true);
#endif
}

void ipc_io_service_impl::associate(ipc_connection * pconn)
{
	assert(pconn != NULL);

	OS_HANDLE oshd = pconn->native_handle();

	//simply reject nonsense input
	if (oshd == INVALID_OS_HANDLE) return;

	ipc_connection* p_conn = get(oshd);

	if (p_conn == NULL) {
#ifdef WIN32
		//first time association: register into IO completion port system.
		//we use the handle as completion key directly to be more compatible with linux/epoll
		//though this is not the best solution
		if (NULL == CreateIoCompletionPort(oshd, m_h_io_compl_port, (ULONG_PTR)oshd, 0))
			THROW_SYSTEM_ERROR("associate() CreateIoCompletionPort failed.", GetLastError());
#else
		//linux, just add the fd into epoll
		struct epoll_event event;

		// we choose level-trigger mode, so blocking socket is enough.
		//
		// if we use edge-trigger mode, then we need to drain all available data in cache
		// using non-blocking socket on each epoll-event, and this can bring some difficulty
		// to application parser implementation
		//
		event.events = EPOLLIN | EPOLLRDHUP;
		event.data.fd = pconn->native_handle();
		if(-1 == epoll_ctl(m_epollFd, EPOLL_CTL_ADD, pconn->native_handle(), &event))
			THROW_SYSTEM_ERROR("associate() epoll_ctl failed.", errno);
#endif
	}
	else {
		//do we need remove previous fd?
	}

	//internal association is mutable (although CreateIoCompletionPort can be down only once)
	get(oshd) = pconn;
}

void ipc_io_service_impl::unassociate(ipc_connection * pconn)
{
	assert(pconn != NULL);
	OS_HANDLE oshd = pconn->native_handle();

	//simply reject nonsense input
	if (oshd == INVALID_OS_HANDLE) return;

#ifdef WIN32
	//no way to un-associate unless we close the file handle
#else
	epoll_ctl(m_epollFd, EPOLL_CTL_DEL ? ? ? , pconn->native_handle(), &event);
#endif

	//remove from cache
	erase(oshd);
}

OS_HANDLE ipc_io_service_impl::native_handle(void)
{
#ifdef WIN32
	return m_h_io_compl_port;
#else
	return m_epollFd;
#endif
}



void ipc_io_service_impl::run()
{
#ifdef WIN32
		m_exit.store(false);
		//this thread will exit when m_exit is set
		// or the CompletionPort is closed
		while (!m_exit.load())
		{
			// the I/O completion port will post event on each low-level packet arrival
			// which means the actuall NumberOfBytes still may less than required.
			//
			// but most time it is of the same size as sender's write buffer length
			//
			DWORD NumberOfBytes;
			ULONG_PTR CompletionKey;
			LPOVERLAPPED  lpOverlapped;
			BOOL bSuccess = GetQueuedCompletionStatus(m_h_io_compl_port,
				&NumberOfBytes,
				&CompletionKey,
				&lpOverlapped,
				m_epollTimeout);

			//Only GetLastError() on failure
			DWORD dwErr = bSuccess ? 0 : GetLastError();

			if (!bSuccess && ERROR_ABANDONED_WAIT_0 == dwErr)
				break;

			//Success
			if (lpOverlapped == NULL) {
				continue;
			}

			if (0) {
				std::error_code ec(dwErr, std::system_category());
				printf(">>>> %s GetQueuedCompletionStatus() returns bSuccess=%d, NumberOfBytes=%d, lpOverlapped=%p GetLastError=%d %s\n", __FUNCTION__,
					bSuccess, NumberOfBytes, lpOverlapped, dwErr, ec.message().c_str());
			}

			if (lpOverlapped->hEvent) {
				//a sync operation, skip callback
				continue;
			}

			//lpOverlapped  is not null
			// CompletionKey is the file handle
			// we don't need a map because this Key is the Callback
			OS_HANDLE oshd = static_cast<OS_HANDLE>((void*)CompletionKey);

			ipc_connection * pconn = get(oshd);
			assert(pconn);

			//do not let exception from one connection terminate whole service thread!
			try {
				pconn->notify(dwErr, NumberOfBytes, lpOverlapped);
			}
			catch (std::exception & e) {
				std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << e.what() << std::endl;
			}
			catch (...) {
				std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << "Unknown" << std::endl;
			}
		}
#else
		m_exit.store(false);
		//this thread will exit when m_exit is set
		// or the CompletionPort is closed
		while (!m_exit.load())
		{
			struct epoll_event events[m_maxEpollEvents];
			int numEvents = epoll_wait(m_epollFd, events, m_maxEpollEvents, m_epollTimeout);
			for (int i = 0; i < numEvents; i++)
			{
				int fd = events[i].data.fd;

				ipc_connection * pconn = get(fd);
				assert(pconn);
				//do not let exception from one connection terminate whole service thread!
				try {
					pconn->notify(0, 0, events[i].events);
				}
				catch (std::exception & e) {
					std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << e.what() << std::endl;
				}
				catch (...) {
					std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << "Unknown" << std::endl;
				}
			}
		}
#endif
}

//==========================================================================================================================
//connection imeplmentation

#ifdef WIN32

// this class is an extention to fd/handle
// on Windows, scheduler can locate it through CompletionKey.
// on Linux, this needs derived from fd.
//           https://stackoverflow.com/questions/8175746/is-there-any-way-to-associate-a-file-descriptor-with-user-defined-data
class ipc_connection_win_namedpipe : public ipc_connection
{
public:
	ipc_connection_win_namedpipe(ipc_io_service & service, const std::string & servername);
	~ipc_connection_win_namedpipe();
	virtual void notify(int error_code, int transferred_cnt, void* hint);

	virtual OS_HANDLE native_handle() { return m_oshd; }

	//blocking/sync version(based on async version)
	virtual void read(void * pbuff, const int len, int *lp_cnt);
	virtual void write(void * pbuff, const int len);

	//can be used on duplicate fd(file descriptor) on Linux or File Handle on Windows
	virtual void read(OS_HANDLE &oshd);
	virtual void write(OS_HANDLE oshd);

	//client(blocking is acceptable because usually its short latency)
	virtual int connect(const std::string & dest);
	virtual int listen(void);
	virtual void close(void);

protected:

private:
	ipc_connection_win_namedpipe(ipc_io_service & service, HANDLE handle);

	void wait_for_connection(void);
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
};

ipc_connection_win_namedpipe::ipc_connection_win_namedpipe(ipc_io_service &service, const std::string & servername) :
	ipc_connection(service), 
	m_cache_byte0(0), 
	m_cache_empty(true), 
	m_cache_overlapped{}, 
	m_error_overlapped{}, 
	m_sync_overlapped{},
	m_waitconn_overlapped{},
	m_name(servername)
{
	m_sync_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == m_sync_overlapped.hEvent)
		THROW_SYSTEM_ERROR("ipc_connection_win_namedpipe ctor CreateEvent() failed.", GetLastError());
}

ipc_connection_win_namedpipe::ipc_connection_win_namedpipe(ipc_io_service & service, HANDLE handle) :
	ipc_connection(service), 
	m_cache_byte0(0), 
	m_cache_empty(true), 
	m_cache_overlapped{},
	m_error_overlapped{},
	m_sync_overlapped{}, 
	m_waitconn_overlapped{},
	m_oshd(handle), 
	m_name("")
{
	m_sync_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == m_sync_overlapped.hEvent)
		THROW_SYSTEM_ERROR("ipc_connection_win_namedpipe ctor CreateEvent() failed.", GetLastError());
	m_service.associate(this);	//change association as well
}
ipc_connection_win_namedpipe::~ipc_connection_win_namedpipe()
{
	CloseHandle(m_sync_overlapped.hEvent);
	close();
}

void ipc_connection_win_namedpipe::notify(int error_code, int transferred_cnt, void * hint)
{
	OVERLAPPED * poverlapped = (OVERLAPPED *)hint;

	const char * pevt = "";
	if (poverlapped == &m_cache_overlapped) pevt = "cache";
	if (poverlapped == &m_error_overlapped) pevt = "error";
	if (poverlapped == &m_waitconn_overlapped) pevt = "accept";

	if (poverlapped == &m_sync_overlapped) return;

	if (poverlapped == &m_cache_overlapped ||
		poverlapped == &m_error_overlapped) {

		// scopeguard will always trigger next async read (even in case of exception)
		auto  guard_asyn_cache_read = scopeGuard([this]() {trigger_async_cache_read(); });

		std::error_code ec2;
		if (poverlapped == &m_error_overlapped)
			ec2 = std::error_code(m_error_overlapped.Offset, std::system_category());
		else {
			ec2 = std::error_code(error_code, std::system_category()); ;
		}

		if (transferred_cnt == 1) {
			//fprintf(stderr, ">>>>>>>>>> m_cache_byte0=%c from [%s]  \n", m_cache_byte0, pevt);
			m_cache_empty = false;
		}

		assert(transferred_cnt == 1 || transferred_cnt == 0);

		//cache byte received, 
		if (on_close && ec2.value() == ERROR_BROKEN_PIPE) {
			guard_asyn_cache_read.dismiss();
			on_close(this);
		}
		else
		{
			bool b_new_data_arrived = !m_cache_empty;

			if (on_read)
				on_read(this, ec2, 0);

			//user must atleast read one byte inside the callback
			if (b_new_data_arrived && !m_cache_empty) {
				//no one clear the cache!
				guard_asyn_cache_read.dismiss();
				assert(0);
			}
		}
	}
	else if (poverlapped == &m_waitconn_overlapped) {
		//accept
		if (on_accept) {
			//on windows, the namedpipe handle that accepted the connection
			//will serve the connection directly.
			//
			//In socket, the listening socket is keep listenning, a new socket 
			//will be returned for client connection purpose.
			//
			//to make them consistent, we follow the socket way, return client connection
			//

			//transffer our handle to new wrapper(this is possible, thanks to the mapping that io_service supported)
			ipc_connection_win_namedpipe * pconn = new ipc_connection_win_namedpipe(get_service(), native_handle());

			auto guard_asyn_cache_read = scopeGuard([pconn]() {pconn->trigger_async_cache_read(); });

			listen();//create a new instance of the pipe

					 //user will setup callbacks inside on_accept()
			on_accept(pconn);

			wait_for_connection();
		}
	}
	else
		return;
}

void ipc_connection_win_namedpipe::wait_for_connection(void)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);

	BOOL bRet = ConnectNamedPipe(native_handle(), &m_waitconn_overlapped);
	if (bRet) {
		//connection success, no IO pending, make sure on_accept is called within right thread
		//according to behaviour of ReadFile, completion IO event should be trigered automatically

		//PostQueuedCompletionStatus(get_service().native_handle(), 1, (ULONG_PTR)native_handle(), &m_waitconn_overlapped);
	}
	else {
		DWORD dwErr = GetLastError();
		if (dwErr == ERROR_PIPE_CONNECTED)
			PostQueuedCompletionStatus(get_service().native_handle(), 1, (ULONG_PTR)native_handle(), &m_waitconn_overlapped);

		else if (dwErr != ERROR_IO_PENDING && dwErr != ERROR_SUCCESS) {
			THROW_SYSTEM_ERROR("wait_for_connection ConnectNamedPipe failed.", dwErr);
			//m_error_overlapped.Offset = dwErr;
			//PostQueuedCompletionStatus(get_service().native_handle(), 1, (ULONG_PTR)native_handle(), &m_error_overlapped);
		}
	}
}

//blocking/sync version(based on async version)
void ipc_connection_win_namedpipe::read(void * pvbuff, const int len, int *lp_cnt)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);

	//m_ior_in.setup((char*)pbuff, len, IOTYPE_IN, true);
	unsigned char * pbuff = (unsigned char *)pvbuff;
	int size = len;
	int cnt = 0;
	int bytes_avail = -1;

	//fprintf(stderr, ">");

	//clear the cache if there is data
	if (!m_cache_empty) {
		pbuff[cnt] = m_cache_byte0;
		cnt++;
		m_cache_empty = true;
	}

	if (lp_cnt) {
		DWORD TotalBytesAvail = 0;
		if (PeekNamedPipe(m_oshd, NULL, 0, NULL, &TotalBytesAvail, NULL)) {
			bytes_avail = TotalBytesAvail;
		}
		*lp_cnt = cnt;

		//some mode, return as fast as we can
		if (bytes_avail == 0) return;
	}

	while (cnt < size) {
		DWORD NumberOfBytesTransferred = 0;
		DWORD io_cnt = size - cnt;

		//if we are in async mode and we know bytes in pipe, then we just read so much data that
		//no blocking/async pending will be triggered
		if (bytes_avail > 0)
			io_cnt = std::min<DWORD>(bytes_avail, io_cnt);

		BOOL bRet = ReadFile(native_handle(), pbuff + cnt, io_cnt, &NumberOfBytesTransferred, &m_sync_overlapped);
		//bRet = WriteFile(m_oshd, m_buff + m_cnt, io_cnt, &NumberOfBytesTransferred, &m_sync_overlapped);
		if (bRet) {
			cnt += NumberOfBytesTransferred;
		}
		else {
			DWORD dwErr = GetLastError();
			if (dwErr == ERROR_IO_PENDING) {
				NumberOfBytesTransferred = 0;
				if (GetOverlappedResult(native_handle(), &m_sync_overlapped, &NumberOfBytesTransferred, TRUE)) {
					cnt += NumberOfBytesTransferred;
				}else {
					dwErr = GetLastError();
					if (dwErr != ERROR_SUCCESS) 
						THROW_SYSTEM_ERROR("read GetOverlappedResult() failed.", dwErr);
					//cnt += NumberOfBytesTransferred;	//do we need it? little confusing
					//error_code = dwErr;
				}
				ResetEvent(m_sync_overlapped.hEvent);
			}
			else  if (dwErr != ERROR_SUCCESS) {
				THROW_SYSTEM_ERROR("read ReadFile() failed.", dwErr);
			}
		}

		if (lp_cnt != NULL && cnt > 0) {
			*lp_cnt = cnt;
			break;
		}
	};
	//fprintf(stderr, "<");
	
	return;
}
void ipc_connection_win_namedpipe::write(void * pvbuff, const int len)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);

	unsigned char * pbuff = (unsigned char *)pvbuff;
	int size = len;
	int cnt = 0;
	
	while (cnt < size) {
		DWORD NumberOfBytesTransferred = 0;
		BOOL bRet = WriteFile(native_handle(), pbuff + cnt, size - cnt, &NumberOfBytesTransferred, &m_sync_overlapped);
		if (bRet) {
			cnt += NumberOfBytesTransferred;
		}
		else {
			DWORD dwErr = GetLastError();
			if (dwErr == ERROR_IO_PENDING) {
				NumberOfBytesTransferred = 0;
				if (GetOverlappedResult(native_handle(), &m_sync_overlapped, &NumberOfBytesTransferred, TRUE)) {
					cnt += NumberOfBytesTransferred;
				}
				else {
					dwErr = GetLastError();
					if (dwErr != ERROR_SUCCESS)
						THROW_SYSTEM_ERROR("write GetOverlappedResult() failed.", dwErr);
					//cnt += NumberOfBytesTransferred;	//do we need it? little confusing
				}
				ResetEvent(m_sync_overlapped.hEvent);
			}
			else if (dwErr != ERROR_SUCCESS) {
				THROW_SYSTEM_ERROR("write WriteFile() failed.", dwErr);
			}
		}
	};

	return;
}
//can be used on duplicate fd(file descriptor) on Linux or File Handle on Windows
void ipc_connection_win_namedpipe::read(OS_HANDLE &hDstHandle)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);
	DWORD SrcProcessID;
	OS_HANDLE hSrcHandle;
	
	read(&SrcProcessID, sizeof(SrcProcessID), NULL);
	read(&hSrcHandle, sizeof(hSrcHandle), NULL);

	HANDLE hSrcProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, SrcProcessID);
	if (hSrcProcess == NULL)
		THROW_SYSTEM_ERROR("read(OS_HANDLE) OpenProcess failed.", GetLastError());

	if (!DuplicateHandle(hSrcProcess,				//hSourceProcessHandle
		(HANDLE)hSrcHandle,				//hSourceHandle
		GetCurrentProcess(),		//hTargetProcessHandle
		&hDstHandle,					//lpTargetHandle
		0,							//dwDesiredAccess
		FALSE,						//bInheritHandle
		DUPLICATE_SAME_ACCESS))		//dwOptions
	{
		THROW_SYSTEM_ERROR("read(OS_HANDLE) DuplicateHandle failed.", GetLastError());
	}

	return;
}
void ipc_connection_win_namedpipe::write(OS_HANDLE oshd)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);
	DWORD CurProcessID = GetCurrentProcessId();

	//send processID alone with handle
	write(&CurProcessID, sizeof(CurProcessID));
	write(&oshd, sizeof(oshd));

	return;
}

void ipc_connection_win_namedpipe::trigger_async_cache_read(void)
{
	assert(m_cache_empty == true);

	DWORD NumberOfBytesTransferred = 0;
	m_cache_overlapped = {};	//do it async 
	m_cache_empty = true;
	BOOL bRet = ReadFile(native_handle(), &m_cache_byte0, 1, NULL, &m_cache_overlapped);
	if (bRet) {
		//for FILE_FLAG_OVERLAPPED, completion IO event will trigger even when the READ opeartion is completed on the spot. 
		//PostQueuedCompletionStatus(get_service().native_handle(), 1, (ULONG_PTR)native_handle(), &m_cache_overlapped);
	}
	else {
		DWORD dwErr = GetLastError();
		if (dwErr != ERROR_IO_PENDING) {
			//error occurs, make sure on_read() callback running inside io_service() thread, like epoll does
			//std::error_code ec(dwErr, std::system_category());
			//fprintf(stderr, "trigger_async_cache_read() returns %d:%s\n", dwErr, ec.message().c_str());
			m_error_overlapped.Offset = dwErr;
			PostQueuedCompletionStatus(get_service().native_handle(), 0, (ULONG_PTR)native_handle(), &m_error_overlapped);
		}
	}
}

int ipc_connection_win_namedpipe::connect(const std::string & dest)
{
	HANDLE oshd = CreateFile(dest.c_str(),   // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		FILE_FLAG_OVERLAPPED,              // default attributes 
		NULL);          // no template file 
	if (oshd == INVALID_HANDLE_VALUE)
		THROW_SYSTEM_ERROR("connect() CreateFile failed.", GetLastError());

	m_oshd = oshd;
	m_service.associate(this);
	trigger_async_cache_read();
	return 0;
}

int ipc_connection_win_namedpipe::listen(void)
{
	//server
#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096
	HANDLE oshd = CreateNamedPipe(m_name.c_str(),            // pipe name 
		PIPE_ACCESS_DUPLEX |     // read/write access 
		FILE_FLAG_OVERLAPPED,    // overlapped mode 
		PIPE_TYPE_BYTE |      // byte-type pipe 
		PIPE_READMODE_BYTE |  // message-read mode 
		PIPE_WAIT,               // blocking mode 
		PIPE_UNLIMITED_INSTANCES,               // number of instances 
		BUFSIZE * sizeof(TCHAR),   // output buffer size 
		BUFSIZE * sizeof(TCHAR),   // input buffer size 
		PIPE_TIMEOUT,            // client time-out 
		NULL);                   // default security attributes 
	if (oshd == INVALID_HANDLE_VALUE) 
		THROW_SYSTEM_ERROR("listen() CreateNamedPipe failed.", GetLastError());

	// only for ipc server 
	// which do not do IO at all, only async_accept() will be called
	m_oshd = oshd;
	m_service.associate(this);
	wait_for_connection();

	return 0;
}

void ipc_connection_win_namedpipe::close(void)
{
	if (m_oshd != INVALID_OS_HANDLE) {
		//close it
		m_service.unassociate(this);
		CloseHandle(m_oshd);
		m_oshd = INVALID_OS_HANDLE;
	}
}
#endif


#ifdef linux

//Unix Domain Sockets
class ipc_connection_linux_UDS : public ipc_connection
{
public:
	ipc_connection_linux_UDS(ipc_io_service & service, const std::string & serverName);
	~ipc_connection_linux_UDS();

	virtual void notify(int error_code, int transferred_cnt, unsigned long hint);

	virtual OS_HANDLE native_handle() { return m_fd; }

	//blocking/sync version(based on async version)
	virtual int read(void * pbuff, const int len, int *lp_cnt);
	virtual int write(void * pbuff, const int len);

	//client(blocking is acceptable because usually its short latency)
	virtual int connect(const std::string & serverName);
	virtual int listen(void);
	virtual void close(void);
protected:

private:
	ipc_connection_linux_UDS(ipc_io_service & service, int fd);
	bool set_block_mode(bool makeBlocking = true);

	constexpr static const char * CLI_PATH = "/var/tmp/";
	constexpr static const int  m_numListen = 5;

	int 					m_fd;
	const char *			m_name;		//IPC name
	bool 					m_listening;
};

ipc_connection_linux_UDS::ipc_connection_linux_UDS(ipc_io_service &service, const std::string & serverName) :
	ipc_connection(service), m_listening(false)
{
	struct sockaddr_un addr;

	/* create a UNIX domain stream socket */
	if ((m_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		THROW_SYSTEM_ERROR("ipc_connection_linux_UDS ctor : socket() failed.", errno);

	// socket name is not changable, only one bind() can be called
	// so we treat it as a part of resource acquire process (RAll)

	//memset(&addr, 0, sizeof(addr));

	// since its a aggregate C struct, initialize zero can be done this way
	// make sure it do not have irresponsible default constructor or this may fail
	// but since its a C struct, we can safely value-initialize it
	// (https://akrzemi1.wordpress.com/2013/09/10/value-initialization-with-c/)

	addr = {};
	addr.sun_family = AF_UNIX;

	std::string sun_path = serverName;
	if (sun_path.empty()) {
		//client socket do not have server name, build one for it
		std::ostringstream stringStream;
		stringStream << CLI_PATH;
		stringStream << std::setw(5) << std::setfill('0') << getpid();
		sun_path = stringStream.str();
	}

	::unlink(sun_path.c_str());

	int cnt = string_copy(addr.sun_path, sizeof(addr.sun_path), sun_path);
	int len = offsetof(sockaddr_un, sun_path) + cnt + 1;

	if (::bind(m_fd, (struct sockaddr*)&addr, len) < 0) 
		THROW_SYSTEM_ERROR("ipc_connection_linux_UDS ctor : bind() failed.", errno);

	service.associate(this);
}
ipc_connection_linux_UDS::ipc_connection_linux_UDS(ipc_io_service & service, int fd) :
	ipc_connection(service), m_listening(false), m_fd(fd)
{
	service.associate(this);
}
ipc_connection_linux_UDS::~ipc_connection_linux_UDS()
{
	m_service.unassociate(this);
	close();
}

void ipc_connection_linux_UDS::notify(int error_code, int transferred_cnt, unsigned long hint)
{
	//printf(">>>>>>>>>>>>>>> notify : %s,%s\n",hint&EPOLLIN?"EPOLLIN":"",hint&EPOLLRDHUP?"EPOLLRDHUP":"");

	//if we are listening socket, 		do on_accept on EPOLLIN
	//
	//	to be consistent with common behavior, we just
	//  accept the connection and return connected connection to user.
	//
	//if we are communication socket, 	do on_read on EPOLLIN
	//
	if (m_listening && (hint & EPOLLIN)) {
		int clifd, err, rval;
		struct stat statbuf;
		struct sockaddr_un addr;

		socklen_t len = sizeof(addr);
		if ((clifd = ::accept(m_fd, (struct sockaddr *)&addr, &len)) < 0) {
			//HError("accept error: %s\n", strerror(errno));
			THROW_SYSTEM_ERROR("notify() accept failed.", errno);
			return;     /* often errno=EINTR, if signal caught */
		}

		if (on_accept) {
			/* obtain the client's uid from its calling address */
			len -= offsetof(struct sockaddr_un, sun_path); /* len of pathname */
			addr.sun_path[len] = 0;           /* null terminate */

			if (stat(addr.sun_path, &statbuf) < 0) {
				//HError("stat error: %s\n", strerror(errno));
				::close(clifd);
				THROW_SYSTEM_ERROR("notify() stat failed.", errno);
				return;
			}

			if (S_ISSOCK(statbuf.st_mode) == 0) {
				//HError("S_ISSOCK error: %s\n", strerror(errno));
				//rval = -3;      /* not a socket */
				::close(clifd);
				THROW_SYSTEM_ERROR("notify() S_ISSOCK failed.", errno);
				return;
			}
			__uid_t uid = statbuf.st_uid;   /* return uid of caller */
			unlink(addr.sun_path);        /* we're done with pathname now */

			ipc_connection_linux_UDS * pconn = new ipc_connection_linux_UDS(get_service(), clifd);

			on_accept(static_cast<ipc_connection *>(pconn));
		}

	}
	else if (hint & EPOLLRDHUP) {
		if (on_close)
			on_close(this);
	}
	else if (hint & EPOLLIN) {
		//new data arrived
		std::error_code ec(error_code, std::system_category());
		if (on_read)
			on_read(this, ec, transferred_cnt);
	}
}

int ipc_connection_linux_UDS::connect(const std::string & serverName)
{
	int len, err, rval;
	struct sockaddr_un addr = { 0 };

	/* fill socket address structure with server's address */
	addr.sun_family = AF_UNIX;

	int cnt = string_copy(addr.sun_path, sizeof(addr.sun_path), serverName);

	len = offsetof(struct sockaddr_un, sun_path) + cnt + 1;

	if (::connect(m_fd, (struct sockaddr *)&addr, len) < 0) {
		THROW_SYSTEM_ERROR("connect() connect failed.", errno);
		//HError("connect error: %s\n", strerror(errno));
		return errno;
	}

	return 0;
}
int ipc_connection_linux_UDS::listen(void)
{
	if (!m_listening) {
		if (::listen(m_fd, m_numListen) < 0) {
			return errno;
		}
		m_listening = true;
	}
	return 0;
}

int ipc_connection_linux_UDS::read(void * buffer, const int bufferSize, int * lp_cnt)
{
	int err = 0;

	if (m_fd <= 0 || !buffer || bufferSize <= 0) {
		return -1;
	}

	char *ptr = static_cast<char*>(buffer);

	set_block_mode(lp_cnt == NULL);

	int leftBytes = bufferSize;
	while (leftBytes > 0) {
		int readBytes = ::read(m_fd, ptr, leftBytes);
		if (readBytes == 0) {
			/* reach EOF */
			break;
		}
		else if (readBytes < 0) {
			if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
				err = errno;
				fprintf(stderr, "read failed with %d: %s\n", errno, strerror(errno));
				break;
			}
			readBytes = 0;
		}

		ptr += readBytes;
		leftBytes -= readBytes;

		if (lp_cnt && readBytes > 0)
			break;
	}

	if (lp_cnt)
		*lp_cnt = (bufferSize - leftBytes);

	//fprintf(stderr,"read %d bytes, with errno = %d: %s\n", bufferSize - leftBytes, err, strerror(err));
	return err;
}

int ipc_connection_linux_UDS::write(void * buffer, const int bufferSize)
{
	int err = 0;

	if (m_fd <= 0 || !buffer || bufferSize <= 0) {
		return -1;
	}

	char *ptr = static_cast<char*>(buffer);

	int leftBytes = bufferSize;
	while (leftBytes > 0) {
		int writeBytes = ::write(m_fd, ptr, leftBytes);
		if (writeBytes < 0) {
			if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
				err = errno;
				fprintf(stderr, "write failed with %d: %s\n", errno, strerror(errno));
				break;
			}
			writeBytes = 0;
		}

		ptr += writeBytes;
		leftBytes -= writeBytes;
	}

	return err;
}

bool ipc_connection_linux_UDS::set_block_mode(bool makeBlocking)
{
	int curFlags = fcntl(m_fd, F_GETFL, 0);

	int newFlags = 0;
	if (makeBlocking) {
		newFlags = curFlags & (~O_NONBLOCK);
	}
	else {
		newFlags = curFlags | O_NONBLOCK;
	}

	int status = fcntl(m_fd, F_SETFL, newFlags);
	if (status < 0) {
		return false;
	}
	return true;
}

void ipc_connection_linux_UDS::close(void)
{
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
}
#endif



ipc_connection::Ptr ipc_connection::create(ipc_io_service * p_service, const char* ipc_type, const char * server_name)
{
	assert(server_name);
	assert(p_service);
#ifdef WIN32
	if (strcmp(ipc_type, "") == 0 ||
		strcmp(ipc_type, "win_namedpipe") == 0) {
		return ipc_connection::Ptr(new ipc_connection_win_namedpipe(*p_service, server_name));
	}
#endif
#ifdef linux
	if (strcmp(ipc_type, "") == 0 ||
		strcmp(ipc_type, "linux_UDS") == 0) {
		return ipc_connection::Ptr(new ipc_connection_linux_UDS(*p_service, server_name));
	}
#endif
	return NULL;
}
