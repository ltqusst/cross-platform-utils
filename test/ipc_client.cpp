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
#include "utils.h"
#include "ipc.h"
#include "ipc_common.h"

#include <future>




static cross::EResult client(int argc, char * argv[])
{
	using namespace cross;

	//auto		p_service = ipc_io_service::create();
	auto poller = std::make_shared<ipc_connection_poller>();
	if(!poller)
		return anERROR(-1)<<"poller create failed.";

	//client by default has no name
	auto client = std::make_shared<ipc_conn>(poller.get());
	if(!client)
		return anERROR(-1)<<"client create failed.";

	std::atomic<bool> b_exit(false);
	
	std::packaged_task<EResult(ipc_connection *)> read_by_poller;
	//===============================================================
	// poller thread for receiving responses from server
	std::thread th([&]{
		char buff_rx[1024];

		//typical polling thread
		while(!b_exit.load()){

			ipc_poll_event evt;
			EResult err = poller->wait(&evt);

			if(err){
				std::cerr << "poller->wait(evt) error:" << err.message() << std::endl;
				continue;
			}

			//no error, but also no event (possibly timeout)
			if(evt.pconn == NULL)
				continue;

			switch(evt.e)
			{
				case ipc_poll_event::event::POLLIN:
				{
					if (read_by_poller.valid()) {
						read_by_poller(evt.pconn);
					}
					else {
						//no callbacks, do defaults, just show them:
						int len;
						err = evt.pconn->read(buff_rx, sizeof(buff_rx), &len);
						if (err) {
							std::cerr << err.message() << std::endl;
							continue;
						}

						std::cout << "Server:";
						for (int i = 0; i < len; i++) {
							std::cout << buff_rx[i];
							if (i >= 16) {
								std::cout << "...(" << len << ") bytes total";
								break;
							}
						}
						std::cout << std::endl;
					}
				}
				break;
				case ipc_poll_event::event::POLLHUP:
				{
					std::cout << "Server closed:" << evt.pconn->native_handle() << std::endl;
					evt.pconn->close();
					break;
				}
				break;
			}
		}
	});

	// in any case, thread should exit
	auto g1 = make_scopeGuard([&]{b_exit.store(true); th.join();});
		
	//use std::cin
	std::shared_ptr<std::istream> pis = std::shared_ptr<std::istream>(&std::cin, [](std::istream *) {});
	std::string str_cmds;
	const char * cmds = nullptr;
	if(argc > 1){
		//use string args for istream
		//each arg as a new line input
		std::stringstream ss;
		for (int i = 1; i < argc; i++)
			ss <<argv[i]<<"\n";
		str_cmds = ss.str();
		pis = std::make_shared<std::stringstream>(str_cmds.c_str());
	}

	cross::EResult err = 0;

	char buff_tx[1024];
	char buff_rx[1024];


	while (!err) {
		std::cout << "Input:" << std::flush;

		//scanf_s("%10s", buff_tx, (unsigned)sizeof(buff_tx));
		pis->getline(buff_tx, sizeof(buff_tx));

		if (!(*pis))
			break;

		if (strlen(buff_tx) == 0)
			continue;

		std::cout << "[len=" << strlen(buff_tx) << "]" << buff_tx << std::endl;
		
		if (strcmp(buff_tx, "start") == 0)
		{
			char path[1024];
			pis->getline(path, sizeof(path));
			char * argv[] = { path, NULL };
			err = create_daemon(argv);

			if (err) break;

			//give server some time to start
			//using namespace std::chrono_literals; //C++14
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		else
		if (strcmp(buff_tx, "conn") == 0)
		{
			std::cout << "Conneting to " << servername << "\n";
			client->close();
			err = client->connect(servername);
			if (err) break;

			std::cout << "client " << client->native_handle() << " Connected\n";
		}
		else
		if (strcmp(buff_tx, "noconn") == 0) {
			client->close();
			auto err1 = client->connect(servername);
			if (err1) {
				std::cout << err.message() << std::endl;
				err = 0;
			}
			else {
				err = anERROR(-1) << "connected when supposed to fail";
				break;
			}
		}
		else
		if (strcmp(buff_tx, "exit") == 0)
		{
			err = client->write(buff_tx, strlen(buff_tx));
			//give server some time to stop
			//using namespace std::chrono_literals;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		else
		if (strcmp(buff_tx, "echo1") == 0 ||
			strcmp(buff_tx, "echo2") == 0)
		{
			//echo test, server supposed to send same message back
			//echo1 will block poller events and receive data in a SYNC/BLOCKING way
			//echo2 will rely on poller's loop to receive data
			int echo_id = buff_tx[4] - '0';

			int cnt = 0;
			(*pis) >> cnt;	//read a number as times

			int progress = 0;
			for (int i = 0; i < cnt; i++) {
				int len = (rand() % sizeof(buff_tx)) + 1;

				std::shared_ptr<ipc_connection_poll_blocker> lk;
				if (echo_id == 1) {
					//for doing acitive read (prevent the poller thread to received data)
					lk = client->make_blocker_POLLIN();
				}

				//rely on poller's loop to receive data by install callbacks
				std::future<EResult> future_echo2;
				if (echo_id == 2) {
					// create the task
					read_by_poller = std::packaged_task<EResult(ipc_connection *)>([&](ipc_connection * pconn) {
						return pconn->read(buff_rx, len);
					});
					future_echo2 = read_by_poller.get_future();
				}

				err = client->write("echo", 4);
				if (err) break;

				err = client->write(&len, sizeof(len));
				if (err) break;

				for (int k = 0; k < len; k++)
					buff_tx[k] = rand() & 0xFF;

				err = client->write(buff_tx, len);
				if (err) break;

				if (echo_id == 1){
					//note: this read is not inside poller's loop or thread context,
					//      so poller for this connection is blocked temporarily to prevent
					//      uncertainty.
					err = client->read(buff_rx, len);
				}
				if (echo_id == 2){
					//wait async callbacks in poller's loop done.
					err = future_echo2.get();

					//must clear the task
					read_by_poller = std::packaged_task<EResult(ipc_connection *)>();
				}
				if (err) break;

				for (int k = 0; k < len; k++, progress++)
					if (buff_rx[k] != buff_tx[k]) {
						err = anERROR(-1) << "echo failed at byte " << progress;
						break;
					}

				if (err) break;

				std::cout << "echo " << i << "/" << cnt << " of len " << len << " success." << std::endl;
			}
		}
		else
		if (strcmp(buff_tx, "tst1") == 0)
		{
			//brute force test:
			//	send 1000 random length packet containinig continous numbers
			if (err = client->write(buff_tx, 4))
				break;

			int total = 1024 * 1024 * 100;
			if (err = client->write(&total, sizeof(total)))
				break;

			int id = 0;
			while (id < total)
			{
				int len = (rand() % sizeof(buff_tx)) + 1;
				//int len = sizeof(buff_tx);
				int k;
				for (k = 0; k < len && id < total; k++, id++) {
					buff_tx[k] = id;
				}
				if (err = client->write(buff_tx, k))
					break;
			}
			if (err) break;
			continue;
		}
		else
			if (err = client->write(buff_tx, strlen(buff_tx)))
				break;
	}
	printf("Over\n");
	return err;
}



cross::EResult test(int i)
{
	if(i < 0)
	{
		return anERROR(-1) << "param i=" << i << " is negative! ";
	}

	if(i>100){
		return anERROR(-100) <<"param i=" << i << " is too big! ";
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
	/*
	EResult err;

	std::cout << "before call test(1)" << std::endl;

	auto t1 = std::chrono::steady_clock::now();
	for(int i=0;i<100000;i++)
		test(1);
	auto t2 = std::chrono::steady_clock::now();
	std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count() << "MS\n";

	std::cout << "before call test(101)" << std::endl;
	err = test(101);

	err.ignore();

	if(err){
		std::cout << "GOT an Error:" << err.message() << std::endl;
	}

	//use copy ctor syntax will enable RVO return-value-optimization
	std::cout << "before call test(-1)" << std::endl;
	err = test(-1);

	if(err.error()){
		std::cout << "GOT error=" << err.error() << std::endl;
	}
*/

	cross::EResult err = client(argc,argv);

	std::cerr << err.message() << std::endl;
	std::cerr << "exit status is:" << -err << std::endl;

	return -err;
}

