
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

namespace cross{



//poller is better than service because more flexible and no callback
ipc_connection_poller::ipc_connection_poller(){
#ifdef WIN32
	m_h_io_compl_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (m_h_io_compl_port == NULL)
		THROW_SYSTEM_ERROR("ipc_io_service ctor: CreateIoCompletionPort failed.", GetLastError());
#endif
#ifdef linux
	m_epollFd = epoll_create(m_epollSize);
	if (m_epollFd < 0)
		THROW_SYSTEM_ERROR("Poll ctor: epoll_create failed.", errno);
#endif
}

OS_HANDLE ipc_connection_poller::native_handle(void){
#ifdef WIN32
	return m_h_io_compl_port;
#endif
#ifdef linux
	return m_epollFd;
#endif
}

ipc_connection_poller::~ipc_connection_poller(){
#ifdef WIN32
	CloseHandle(m_h_io_compl_port);
#else
	close(m_epollFd);
#endif
}

void ipc_connection_poller::add(ipc_connection * pconn){
	assert(pconn != NULL);

	OS_HANDLE oshd = pconn->native_handle();

	//simply reject nonsense input
	if (oshd == INVALID_OS_HANDLE) return;

	ipc_connection* p_conn = m_mapper.get(oshd);

	if (p_conn == NULL) {
#ifdef WIN32
		//first time association: register into IO completion port system.
		//we use the handle as completion key directly to be more compatible with linux/epoll
		//though this is not the best solution
		if (NULL == CreateIoCompletionPort(oshd, m_h_io_compl_port, (ULONG_PTR)oshd, 0))
			THROW_SYSTEM_ERROR("add() CreateIoCompletionPort failed.", GetLastError());
#else
		//linux, just add the fd into epoll
		// we choose level-trigger mode, so blocking socket is enough.
		//
		// if we use edge-trigger mode, then we need to drain all available data in cache
		// using non-blocking socket on each epoll-event, and this can bring some difficulty
		// to application parser implementation
		//
		struct epoll_event event;
		event.events = EPOLLIN | EPOLLRDHUP;
		event.data.fd = pconn->native_handle();
		if(-1 == epoll_ctl(m_epollFd, EPOLL_CTL_ADD, pconn->native_handle(), &event))
			THROW_SYSTEM_ERROR("add() epoll_ctl failed.", errno);
#endif
	}
	else {
		//do we need remove previous fd?
	}

	//internal association is mutable (although CreateIoCompletionPort can be down only once)
	m_mapper.get(oshd) = pconn;
}


void ipc_connection_poller::del(ipc_connection * pconn){
	assert(pconn != NULL);
	OS_HANDLE oshd = pconn->native_handle();

	//simply reject nonsense input
	if (oshd == INVALID_OS_HANDLE) return;

#ifdef WIN32
	//no way to un-associate unless we close the file handle
#else
	epoll_ctl(m_epollFd, EPOLL_CTL_DEL, oshd, NULL);
#endif

	//remove from cache
	m_mapper.erase(oshd);
}


EResult ipc_connection_poller::wait(ipc_poll_event * pevt){

	if(pevt == nullptr)
		return anERROR(-1) << "pevt is null";

	//empty the callbacks(errors will be silient)
	while (!m_prewait_callbacks.empty()) {
		std::function<void()> func = m_prewait_callbacks.front();
		m_prewait_callbacks.pop();
		func();
	}

#ifdef WIN32
		// the I/O completion port will post event on each low-level packet arrival
		// which means the actuall NumberOfBytes still may less than required.
		//
		// but most time it is of the same size as sender's write buffer length
		//
		*pevt  = {};

		DWORD NumberOfBytes;
		ULONG_PTR CompletionKey;
		LPOVERLAPPED  lpOverlapped;
		BOOL bSuccess = GetQueuedCompletionStatus(m_h_io_compl_port,
			&NumberOfBytes,
			&CompletionKey,
			&lpOverlapped,
			m_epollTimeout);

		DWORD dwErr = bSuccess ? 0 : (::GetLastError());

		if (WAIT_TIMEOUT == dwErr)
			return 0;//no result on timeout

		if (ERROR_ABANDONED_WAIT_0 == dwErr)
			return anERROR(-1, dwErr) << "Completion port handle closed.";

		if (lpOverlapped == NULL) 
			return anERROR(-2, dwErr) << "GetQueuedCompletionStatus() failed.";

		//SYNC OP failure also cause IO port return, ignore that
		if (lpOverlapped && lpOverlapped->hEvent)
			return 0;

		if (0) {
			printf(">>>> %s GetQueuedCompletionStatus() returns bSuccess=%d, NumberOfBytes=%d, lpOverlapped=%p \n", __FUNCTION__,
				bSuccess, NumberOfBytes, lpOverlapped);
		}

		//lpOverlapped  is not null
		// CompletionKey is the file handle
		// we don't need a map because this Key is the Callback
		OS_HANDLE oshd = static_cast<OS_HANDLE>((void*)CompletionKey);

		ipc_connection * pconn = m_mapper.get(oshd);

		assert_line(pconn);
		//do not let exception from one connection terminate whole service thread!
		try {
			pconn->notify(dwErr, NumberOfBytes, (uintptr_t)lpOverlapped, pevt);
		}
		catch (std::exception & e) {
			std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << e.what() << std::endl;
			return anERROR(-3) << e.what();
		}
		catch (...) {
			std::cerr << std::endl << "Exception in " __FUNCTION__ ": " << "Unknown" << std::endl;
			return anERROR(-4) << "Unknown exception";
		}
		
		return 0;
#endif

#ifdef linux
		*pevt = {};

		// Block SIGPIPE signal because remote peer may exit abnormally
		static std::once_flag flag1;
		std::call_once(flag1, [](){
			sigset_t set;
			sigemptyset(&set);
			sigaddset(&set, SIGPIPE);
			int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
			if (s != 0)
				THROW_SYSTEM_ERROR("pthread_sigmask failed", s);		//TODO
		});

		struct epoll_event event;
		int numEvents = epoll_wait(m_epollFd, &event, 1, m_epollTimeout);

		if(numEvents < 0)
			return anERROR(-3, errno) << "epoll_wait() error.";

		if(numEvents == 1){
			int fd = event.data.fd;

			ipc_connection * pconn = m_mapper.get(fd);
			assert_line(pconn);
			//do not let exception from one connection terminate whole service thread!
			try {
				pconn->notify(0, 0, event.events, pevt);
			}
			catch (std::exception & e) {
				std::cerr << std::endl << "Exception in " << __FUNCTION__ << ": " << e.what() << std::endl;
				return anERROR(-1) << e.what();
			}
			catch (...) {
				std::cerr << std::endl << "Exception in " << __FUNCTION__ << ": " << "Unknown" << std::endl;
				return anERROR(-2) << "Unknown exception";
			}
		}

		return 0;
#endif
}

//==========================================================================================================================
//connection imeplmentation

#ifdef WIN32


ipc_connection_win_namedpipe::ipc_connection_win_namedpipe(ipc_connection_poller * p) :
	ipc_connection(p), 
	m_cache_byte0(0), 
	m_cache_empty(true), 
	m_cache_overlapped{}, 
	m_error_overlapped{}, 
	m_sync_overlapped{},
	m_waitconn_overlapped{},
	m_name(),
	m_state(state::EMPTY)
{
	m_sync_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == m_sync_overlapped.hEvent)
		THROW_SYSTEM_ERROR("ipc_connection_win_namedpipe ctor CreateEvent() failed.", GetLastError());
}

ipc_connection_win_namedpipe::~ipc_connection_win_namedpipe()
{
	CloseHandle(m_sync_overlapped.hEvent);
	close();
}

void ipc_connection_win_namedpipe::notify(int error_code, int transferred_cnt, uintptr_t hint, ipc_poll_event * p_evt)
{
	OVERLAPPED * poverlapped = (OVERLAPPED *)hint;

	if (poverlapped == &m_error_overlapped)
	{
		int ec = m_error_overlapped.Offset;
		if (ec == ERROR_BROKEN_PIPE) {
			p_evt->e = ipc_poll_event::event::POLLHUP;
			p_evt->pconn = this;
		}
		else {
			assert(0);
		}
	}
	else if (poverlapped == &m_cache_overlapped)
	{
		if (error_code == 0) {
			assert(transferred_cnt == 1);
			//fprintf(stderr, ">>>>>>>>>> m_cache_byte0=%c from [%s]  \n", m_cache_byte0, pevt);
			m_cache_empty = false;
			p_evt->e = ipc_poll_event::event::POLLIN;
			p_evt->pconn = this;
			m_poller->queue_prewait_callback([this]() {
				// prewait_callback will happen before next poller wait() cycle , 
				// user of the connection is supposed to have done all read with it 
				// and will suspending ifself until next byte arrives. 
				// so that is the point we will trigger async 1byte cache read operation.

				//trigger_async_cache_read is designed to not throw or fail, it will post error
				//to completion IO port instead.
				this->trigger_async_cache_read();
			});
		}
		else if (error_code == ERROR_BROKEN_PIPE) {
			p_evt->e = ipc_poll_event::event::POLLHUP;
			p_evt->pconn = this;
		}
		else {
			assert(0);
		}
	}
	else if (poverlapped == &m_waitconn_overlapped)
	{
		//we do not invent another event because our operation mimics linux socket model
		//and the connection is in LISTEN state, so POLLIN can only means new connection 
		//is arrived.
		p_evt->e = ipc_poll_event::event::POLLIN;	
		p_evt->pconn = this;
	}
}



//blocking/sync version(based on async version)
EResult ipc_connection_win_namedpipe::read(void * pvbuff, const int len, int *lp_cnt)
{
	if (m_oshd == INVALID_OS_HANDLE || pvbuff == nullptr || len == 0)
		return anERROR(-1) << "read() invalid argument m_oshd=" << m_oshd << " pvbuff=" << pvbuff << " len=" << len;

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
		if (bytes_avail == 0) return 0;
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
						return anERROR(-2, dwErr) << "read GetOverlappedResult() failed.";
					//cnt += NumberOfBytesTransferred;	//do we need it? little confusing
					//error_code = dwErr;
				}
				ResetEvent(m_sync_overlapped.hEvent);
			}
			else  if (dwErr != ERROR_SUCCESS) {
				return anERROR(-3, dwErr) << "read ReadFile() failed.";
			}
		}

		if (lp_cnt != NULL && cnt > 0) {
			*lp_cnt = cnt;
			break;
		}
	};
	//fprintf(stderr, "<");
	return 0;
}
EResult ipc_connection_win_namedpipe::write(void * pvbuff, const int len)
{
	if (m_oshd == INVALID_OS_HANDLE || pvbuff == nullptr || len == 0)
		return anERROR(-1) << "write() invalid argument m_oshd=" << m_oshd << " pvbuff=" << pvbuff << " len=" << len;
	
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
						return anERROR(-2, dwErr) << "write GetOverlappedResult() failed.";
					//cnt += NumberOfBytesTransferred;	//do we need it? little confusing
				}
				ResetEvent(m_sync_overlapped.hEvent);
			}
			else if (dwErr != ERROR_SUCCESS) {
				return anERROR(-3, dwErr) << "write WriteFile() failed.";
			}
		}
	};

	return 0;
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
			PostQueuedCompletionStatus(m_poller->native_handle(), 0, (ULONG_PTR)native_handle(), &m_error_overlapped);
		}
	}
}

EResult ipc_connection_win_namedpipe::wait_for_connection(void)
{
	assert_line(native_handle() != INVALID_OS_HANDLE);

	BOOL bRet = ConnectNamedPipe(native_handle(), &m_waitconn_overlapped);
	if (bRet) {
		//connection success, no IO pending, make sure on_accept is called within right thread
		//according to behaviour of ReadFile, completion IO event should be trigered automatically
		//so we don't need do it manually

		//PostQueuedCompletionStatus(get_service().native_handle(), 1, (ULONG_PTR)native_handle(), &m_waitconn_overlapped);
	}
	else {
		DWORD dwErr = GetLastError();
		if (dwErr == ERROR_PIPE_CONNECTED)
			PostQueuedCompletionStatus(m_poller->native_handle(), 1, (ULONG_PTR)native_handle(), &m_waitconn_overlapped);

		else if (dwErr != ERROR_IO_PENDING && dwErr != ERROR_SUCCESS) {
			return anERROR(-1, dwErr) << "wait_for_connection ConnectNamedPipe failed.";
			//m_error_overlapped.Offset = dwErr;
			//PostQueuedCompletionStatus(gm_poller->native_handle(), 1, (ULONG_PTR)native_handle(), &m_error_overlapped);
		}
	}
	return 0;
}

std::ostream& operator<<(std::ostream& s, const ipc_connection_win_namedpipe::state & d)
{
	// Read the image data from the stream into the image
	switch (d) {
	case ipc_connection_win_namedpipe::state::CONN:
		s << "CONN";
		break;
	case ipc_connection_win_namedpipe::state::DISCONN:
		s << "DISCONN";
		break;
	case ipc_connection_win_namedpipe::state::EMPTY:
		s << "EMPTY";
		break;
	case ipc_connection_win_namedpipe::state::LISTEN:
		s << "LISTEN";
		break;
	}
	return s;
}

EResult ipc_connection_win_namedpipe::connect(const std::string & serverName)
{
	if (m_state != state::EMPTY)
		return anERROR(-1) << "m_state=" << m_state << " when connect";

	HANDLE oshd = CreateFile(serverName.c_str(),   // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		FILE_FLAG_OVERLAPPED,              // default attributes 
		NULL);          // no template file 

	if (oshd == INVALID_HANDLE_VALUE)
		return anERROR(-2, ::GetLastError()) << "connect() CreateFile failed.";

	m_oshd = oshd;
	m_state = state::CONN;
	m_poller->add(this);
	trigger_async_cache_read();
	return 0;
}

//listen/accept is triky on Windows, any successful connection will trigger
//an completion event, and the listening handle will becomes the connected
//handle, so we need switch handle to simulate socket
EResult ipc_connection_win_namedpipe::listen(const std::string & serverName)
{
	if (m_state != state::EMPTY)
		return anERROR(-1) << "m_state=" << m_state << " when listen";

	//server
#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096
	HANDLE oshd = CreateNamedPipe(serverName.c_str(),            // pipe name 
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
		return anERROR(-2, ::GetLastError()) << "listen() CreateNamedPipe failed.";

	// only for ipc server 
	// which do not do IO at all, only async_accept() will be called
	m_oshd = oshd;
	m_name = serverName;
	m_state = state::LISTEN;
	m_poller->add(this);

	//start the ConnectNamedPipe ASYNC call 
	EResult err = wait_for_connection();
	if (err)
		return anERROR(-3, err.error_sys()) << "wait_for_connection failed.";

	return 0;
}

//when accept is called, the listener is actually already connected
//so we just swap the underlying HANDLE with the listener and tell 
//listener to listen again
EResult ipc_connection_win_namedpipe::accept(ipc_connection * listener)
{
	ipc_connection_win_namedpipe * plis = dynamic_cast<ipc_connection_win_namedpipe *>(listener);

	if(!plis)
		return anERROR(-1) << "listener is not of type ipc_connection_win_namedpipe";

	if (m_state != state::EMPTY)
		return anERROR(-2) << "m_state=" << m_state << " when accept";

	if (plis->m_state != state::LISTEN)
		return anERROR(-3) << "listener->m_state=" << plis->m_state << " when accept";

	//stole from listener
	m_oshd = plis->m_oshd;
	m_state = state::CONN;
	m_name = plis->m_name;
	m_poller->add(this);		//change the HANDLE<->object mapping

	trigger_async_cache_read();	//trigger ASYNC 1byte cache read for connected 

	//put listener to listen again
	plis->m_oshd = NULL;
	plis->m_state = state::EMPTY;

	EResult err;
	err = plis->listen(plis->m_name);
	if (err) 
		return anERROR(-4, err.error_sys()) << "re-listen on " << plis->m_name << " failed";

	return 0;
}

void ipc_connection_win_namedpipe::close(void)
{
	if (m_oshd != INVALID_OS_HANDLE) {
		//close it
		m_poller->del(this);
		CloseHandle(m_oshd);
		m_oshd = INVALID_OS_HANDLE;
	}
	m_state = state::EMPTY;
}
#endif


#ifdef linux
ipc_connection_linux_UDS::ipc_connection_linux_UDS(ipc_connection_poller * p) :
	ipc_connection(p), m_fd(-1), m_name(""), m_state(state::EMPTY)
{}
ipc_connection_linux_UDS::~ipc_connection_linux_UDS()
{
	m_poller->del(this);
	close();
}
EResult ipc_connection_linux_UDS::accept(ipc_connection * listener)
{
	if(m_state != state::EMPTY)
		return anERROR(-1) << "state is not EMPTY when connect()";

	struct stat statbuf;
	struct sockaddr_un addr;

	if(listener == nullptr)
		return anERROR(-99) << "listener is null";

	ipc_connection_linux_UDS * plis = dynamic_cast<ipc_connection_linux_UDS*>(listener);
	if(plis == nullptr)
		return anERROR(-98) << "listener is not ipc_connection_linux_UDS";

	socklen_t len = sizeof(addr);
	if ((m_fd = ::accept(listener->native_handle(), (struct sockaddr *)&addr, &len)) < 0)
		return anERROR(-1,errno) << "accept failed";     /* often errno=EINTR, if signal caught */

	auto g1 = make_scopeGuard([&]{close();});

	/* obtain the client's uid from its calling address */
	len -= offsetof(struct sockaddr_un, sun_path); /* len of pathname */
	addr.sun_path[len] = 0;           /* null terminate */

	if (stat(addr.sun_path, &statbuf) < 0)
		return anERROR(-2,errno) << "stat failed";

	if (S_ISSOCK(statbuf.st_mode) == 0)
		return anERROR(-3,errno) << "S_ISSOCK failed";

	__uid_t uid = statbuf.st_uid;   /* return uid of caller */
	unlink(addr.sun_path);        /* we're done with pathname now */

	m_state = state::CONN;
	m_poller->add(this);
	g1.dismiss();

	return 0;
}
void ipc_connection_linux_UDS::notify(int error_code, int transferred_cnt, uintptr_t hint, ipc_poll_event * pevt)
{
	if(m_state == state::EMPTY)
		return;

	//printf(">>>>>>>>>>>>>>> notify : %s,%s\n",hint&EPOLLIN?"EPOLLIN":"",hint&EPOLLRDHUP?"EPOLLRDHUP":"");
	if ((hint & EPOLLRDHUP) || (hint & EPOLLHUP)) {
		pevt->e = ipc_poll_event::event::POLLHUP;
		pevt->pconn = this;
	}
	else if (hint & EPOLLIN) {
		//new data arrived
		pevt->e = ipc_poll_event::event::POLLIN;
		pevt->pconn = this;
		//std::error_code ec(error_code, std::system_category());
	}
}

EResult ipc_connection_linux_UDS::connect(const std::string & serverName)
{
	if(m_state != state::EMPTY)
		return anERROR(-1) << "state is not EMPTY when connect()";

	// create a UNIX domain stream socket
	if ((m_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return anERROR(-2,errno) << "ipc_connection_linux_UDS : socket() failed.";

	auto g1 = make_scopeGuard([&]{close();});

	{
		struct sockaddr_un addr = {0};
		addr.sun_family = AF_UNIX;

		//client socket do not have server name, build one for it
		std::ostringstream stringStream;
		stringStream << CLI_PATH;
		stringStream << std::setw(5) << std::setfill('0') << getpid();
		std::string sun_path = stringStream.str();

		::unlink(sun_path.c_str());

		int cnt = string_copy(addr.sun_path, sun_path);
		int len = offsetof(sockaddr_un, sun_path) + cnt + 1;

		if (::bind(m_fd, (struct sockaddr*)&addr, len) < 0)
			return anERROR(-3, errno) << "ipc_connection_linux_UDS : bind() failed.";
	}

	{
		struct sockaddr_un addr = {0};
		addr.sun_family = AF_UNIX;
		/* fill socket address structure with server's address */
		int cnt = string_copy(addr.sun_path, serverName);
		int len = offsetof(struct sockaddr_un, sun_path) + cnt + 1;

		if (::connect(m_fd, (struct sockaddr *)&addr, len) < 0)
			return anERROR(-4,errno) << "::connect() to " << serverName << " failed.";
	}

	m_state = state::CONN;
	m_poller->add(this);
	g1.dismiss();

	return 0;
}
EResult ipc_connection_linux_UDS::listen(const std::string & serverName)
{
	if(m_state != state::EMPTY)
		return anERROR(-1) << "state is not EMPTY when listen()";

	// create a UNIX domain stream socket
	if ((m_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return anERROR(-2,errno) << "ipc_connection_linux_UDS : socket() failed.";

	auto g1 = make_scopeGuard([&]{close();});

	{
		struct sockaddr_un addr = {0};
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

		int cnt = string_copy(addr.sun_path, sun_path);
		int len = offsetof(sockaddr_un, sun_path) + cnt + 1;

		if (::bind(m_fd, (struct sockaddr*)&addr, len) < 0)
			return anERROR(-3, errno) << "ipc_connection_linux_UDS : bind() failed.";

		if (::listen(m_fd, m_numListen) < 0)
			return anERROR(-4,errno) << "listen failed";
	}

	m_state = state::LISTEN;
	m_poller->add(this);
	g1.dismiss();

	return 0;
}

EResult ipc_connection_linux_UDS::read(void * buffer, const int bufferSize, int * lp_cnt)
{
	if (m_fd <= 0 || !buffer || bufferSize <= 0) {
		assert_line(0);
		//THROW_(std::invalid_argument, "ipc_connection_linux_UDS::read");
		return anERROR(-1) << "read got invalid argument.";
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
				//THROW_SYSTEM_ERROR("read failed ", errno);
				return anERROR(-2,errno) << "read failed.";
				//if exception is banned, we just stop further processing
				break;
			}
			if((errno == EAGAIN || errno == EWOULDBLOCK) && lp_cnt){
				//we can return nothing when no-blocking is required
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
	else
		if(leftBytes > 0)
			return anERROR(-3) << " leftBytes=" << leftBytes;
	//fprintf(stderr,"read %d bytes, with errno = %d: %s\n", bufferSize - leftBytes, err, strerror(err));
	return 0;
}

EResult ipc_connection_linux_UDS::write(void * buffer, const int bufferSize)
{

	if (m_fd <= 0 || !buffer || bufferSize <= 0) {
		//return -1;
		return anERROR(-1) << "invalid argument: m_fd=" << m_fd
						  << " buffer=" << buffer
						  << " bufferSize=" << bufferSize;
	}

	char *ptr = static_cast<char*>(buffer);

	int leftBytes = bufferSize;
	while (leftBytes > 0) {
		int writeBytes = ::write(m_fd, ptr, leftBytes);
		if (writeBytes < 0) {
			if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
				//err = errno;
				//fprintf(stderr, "write failed with %d: %s\n", errno, strerror(errno));
				//THROW_SYSTEM_ERROR("write failed ", errno);
				return anERROR(-1, errno) << "write failed.";
				break;
			}
			writeBytes = 0;
		}

		ptr += writeBytes;
		leftBytes -= writeBytes;
	}

	return 0;
}
void ipc_connection_linux_UDS::read(OS_HANDLE &oshd)
{
	assert(0);
}
void ipc_connection_linux_UDS::write(OS_HANDLE oshd)
{
	assert(0);
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
		m_poller->del(this);
		::close(m_fd);
		m_fd = -1;
	}
	m_state = state::EMPTY;
}
#endif



}
