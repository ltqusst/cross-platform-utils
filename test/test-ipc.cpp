// tipc.cpp:
//
//==================================================================
// Linux/Windows cross-platform IPC based on linux-epoll behavior
// 
// On Windows:
//      I/O Completion Port based NamedPipe
// On Linux:
//      epoll & nonblocking read/write based Unix Domain Sockets
//==================================================================
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


#include "utils.h"
#include "ipc.h"

void server_tst1(ipc_connection * pconn)
{
	char buff_rx[1024];
	int total = 0;
	pconn->read(&total, sizeof(total));

	printf("Client [%p] tst1 on %d bytes:\n", pconn, total);
	int id = 0;
	int64_t ms0 = 0;
	auto time1 = std::chrono::steady_clock::now();
	while (id < total)
	{
		//int len = (rand() % sizeof(buff_rx)) + 1;
		int len = sizeof(buff_rx);

		if (len > total - id) len = total - id;

		pconn->read(buff_rx, len);

		for (int k = 0; k < len && id < total; k++, id++) {
			if (buff_rx[k] != (char)id)
			{
				throw std::runtime_error(std::string("tst1 failed\n"));
			}
		}

		auto time2 = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1);;
		if (ms.count() > ms0 + 1000 || id >= total) {
			ms0 = ms.count();
			std::cerr << "\r" << id << "/" << total << "  (" << (int64_t)id * 100 / total << "%)  " << id / (ms0 * 1024 * 1024 / 1000) << "MB/s";
		}
	}
	std::cerr << " Done\n";
}

#ifdef linux
	const char * servername = "/var/tmp/hddl_service.sock";
	const char * ipc_type = "";	//default
#endif


#ifdef WIN32
	LPTSTR servername = TEXT("\\\\.\\pipe\\mynamedpipe");
	const char * ipc_type = "win_namedpipe";
#endif

static void server(int argc, char * argv[])
{

	auto		p_service = ipc_io_service::create();
	std::thread th([=]{p_service->run();});
	//server mode
	auto								acceptor = ipc_connection::create(p_service.get(), ipc_type, servername);
	std::vector<ipc_connection::Ptr>	connections;

	auto on_close = [&](ipc_connection * pconn) {
		auto it = std::find_if(connections.begin(), connections.end(),
			[&](ipc_connection::Ptr const & p) {return p.get() == pconn; });
		if (it == connections.end()) {
			fprintf(stderr, "Cannot find client connection %p when closing\n", pconn);
			return;
		}
		connections.erase(it);
		//delete pconn; //no need to delete because its already done by smart pointer
		fprintf(stderr, "Client [%p] closed and deleted\n", pconn);
	};

	auto on_read = [&](ipc_connection * pconn, const std::error_code & ec, std::size_t lenx) {
		char buff_tx[1024];
		char buff_rx[1024];
		int len = 0;

		if (ec == std::errc::connection_aborted) {
			fprintf(stderr, "std::errc::connection_aborted\n");
			return;
		}

		if (ec)
			throw std::runtime_error(std::string("on_read failed with ") + ec.message());

		pconn->read(buff_rx, 4, &len);

		if (len == 4 && strncmp(buff_rx, "tst1", 4) == 0) {
			//tst1 test
			server_tst1(pconn);
			return;
		}
		if (len == 4 && strncmp(buff_rx, "exit", 4) == 0) {
			//tst1 test
			p_service->stop();
			return;
		}

		//async mode will return with 0 bytes if no other data in the pipe right now
		int len2 = 0;
		pconn->read(buff_rx + len, sizeof(buff_rx) - len, &len2);
		len += len2;

		//response
		//async_write(buff_tx, 10, std::bind(&ipc_server1::on_write, this, _1, _2));
		printf("Client [%p] got %d bytes:", pconn, len);
		for (int i = 0; i < std::min<>(32, len); i++) {
			printf("%c", buff_rx[i]);
		}

		pconn->read(buff_rx, 1);

		buff_rx[1] = 0;
		printf("And %s:", buff_rx);
		printf("\n");

		string_copy(buff_tx, "GOT IT");
		pconn->write(buff_tx, strlen(buff_tx));
	};

	auto on_accept = [&](ipc_connection * pconn) {
		printf("Client [%p] connected.\n", pconn);
		pconn->on_close = on_close;
		pconn->on_read = on_read;
		connections.push_back(ipc_connection::Ptr(pconn));
	};

	acceptor->on_accept = on_accept;
	acceptor->listen();

	printf("Waitting...\n");
	th.join();
	printf("Over\n");
}

static void client(int argc, char * argv[])
{
	auto		p_service = ipc_io_service::create();
	std::thread th([=]{p_service->run();});

	//client mode

	auto client = ipc_connection::create(p_service.get(), ipc_type);

	char buff_rx[32];
	char buff_tx[1024];

	client->on_read = [&](ipc_connection * pconn, const std::error_code & ec, std::size_t len) {
		printf("Server:");
		int cnt = 0;
		pconn->read(buff_rx, sizeof(buff_rx), &cnt);
		for (int i = 0; i < cnt; i++) {
			printf("%c", buff_rx[i]);
		}
		printf("\n");
	};
	client->on_close = [&](ipc_connection * pconn) {
		printf("Server closed\n");
		exit(0);
	};

	client->connect(servername);

	printf("Start client:\n");

	while (1) {
		printf("Input:"); fflush(stdout);

		//scanf_s("%10s", buff_tx, (unsigned)sizeof(buff_tx));
		std::cin.getline(buff_tx, sizeof(buff_tx));
		if (strcmp(buff_tx, "exit") == 0)
		{
			client->write(buff_tx, strlen(buff_tx));
			break;
		}
		if (strcmp(buff_tx, "tst1") == 0)
		{
			//brute force test:
			//	send 1000 random length packet containinig continous numbers
			//
			client->write(buff_tx, strlen(buff_tx));
			int total = 1024 * 1024 * 100;
			client->write(&total, sizeof(total));
			int id = 0;
			while (id < total)
			{
				int len = (rand() % sizeof(buff_tx)) + 1;
				//int len = sizeof(buff_tx);
				int k;
				for (k = 0; k < len && id < total; k++, id++) {
					buff_tx[k] = id;
				}
				client->write(buff_tx, k);
			}
			continue;
		}

		printf("[len=%d]", strlen(buff_tx));
		client->write(buff_tx, strlen(buff_tx));
	}

	p_service->stop();

	th.join();

	printf("Over\n");
}



EResult test(int i)
{
	if(i < 0)
	{
		return anERROR(1) << "param i=" << i << " is negative! ";
	}

	if(i>100){
		return anERROR(100) <<"param i=" << i << " is too big! ";
	}
	int j=0;
	for(int i=0;i<100;i++)
	{
		j++;
	}

	return 0;
}

int main(int argc, char * argv[])
{
	EResult err;

	std::cout << "before call test(1)" << std::endl;

	auto t1 = std::chrono::steady_clock::now();
	for(int i=0;i<100000;i++)
		test(1);
	auto t2 = std::chrono::steady_clock::now();
	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count() << "MS\n";
/*
	std::cout << "before call test(101)" << std::endl;
	err = test(101);
	if(err.error()){
		std::cout << "GOT an Error:" << err.message() << std::endl;
	}
	//use copy ctor syntax will enable RVO return-value-optimization
	std::cout << "before call test(-1)" << std::endl;
	err = test(-1);

	if(err.error()){
		std::cout << "GOT error=" << err.error() << std::endl;
	}
*/
	return 0;

	if(argc == 1)
		server(argc,argv);
	else
		client(argc,argv);
	return 0;
}

