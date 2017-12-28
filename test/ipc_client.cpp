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

	//===============================================================
	// poller thread for receiving responses from server
	std::thread th([&]{
		char buff_rx[32];
		char buff_tx[1024];

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
					std::cout << "Server:";
					int cnt = 0;
					evt.pconn->read(buff_rx, sizeof(buff_rx), &cnt);
					for (int i = 0; i < cnt; i++) {
						printf("%c", buff_rx[i]);
					}
					std::cout << std::endl;
				}
				break;
				case ipc_poll_event::event::POLLHUP:
				{
					std::cout << "Server closed??" << evt.pconn->native_handle() << std::endl;
					b_exit.store(true);
					break;
				}
				break;
			}
		}
	});
	auto g1 = make_scopeGuard([&]{b_exit.store(true); th.join();});

	std::cout << "Conneting to " << servername << "\n";

	//client mode
	auto err = client->connect(servername);
	if(err){
		std::cout << err.message() << std::endl;
		return err;
	}

	//use empty deleter
	std::shared_ptr<std::istream> pis = std::shared_ptr<std::istream>(&std::cin, [](std::istream *){});
	if(argc > 1){
		pis.reset(new std::stringstream(argv[1]));
	}

	std::cout << "Start client " << client->native_handle() << ":\n" ;
	char buff_tx[1024];
	while (1) {
		std::cout << "Input:" << std::flush;

		//scanf_s("%10s", buff_tx, (unsigned)sizeof(buff_tx));
		pis->getline(buff_tx, sizeof(buff_tx));

		if(!(*pis))
			break;

		if(strlen(buff_tx) == 0)
			continue;

		std::cout << "[len="<< strlen(buff_tx) <<"]" << buff_tx << std::endl;

		if (strcmp(buff_tx, "exit") == 0)
		{
			client->write(buff_tx, strlen(buff_tx));
			break;
		}
		if (strcmp(buff_tx, "tst1") == 0)
		{
			//brute force test:
			//	send 1000 random length packet containinig continous numbers
			client->write(buff_tx, 4);
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

		client->write(buff_tx, strlen(buff_tx));
	}
	printf("Over\n");

	return 0;
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
	client(argc,argv);

	return 0;
}

