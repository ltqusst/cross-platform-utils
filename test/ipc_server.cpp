#include "utils.h"
#include "ipc.h"
#include "ipc_common.h"



//inherit from ipc_conn is supported so user can maintain
//connection state w/o problem

class server_conn:public ipc_conn
{
public:

	server_conn(cross::ipc_connection_poller *p):ipc_conn(p){
		tst1_count = 0;
	}
	~server_conn(){
		std::cout << "Client [" << native_handle() << "] ~server_conn() involked" << std::endl;
	}

	int 													tst1_count;
	int64_t													tst1_id;
	int64_t													tst1_ms0;
	std::chrono::time_point<std::chrono::steady_clock> 		tst1_tim1;

	void on_tst1(){
		char buff_rx[1024];
		int len = sizeof(buff_rx);

		if (tst1_id == 0) {
			tst1_tim1 = std::chrono::steady_clock::now();
			tst1_ms0 = 0;
		}
		
		if (len > tst1_count - tst1_id) len = tst1_count - tst1_id;

		auto err = read(buff_rx, len);
		if (err) {
			std::cerr << "on_tst1() read failed:" << std::endl;
			std::cerr << err.message() << std::endl;
			return;
		}

		for (int k = 0; k < len && tst1_id < tst1_count; k++, tst1_id++) {
			if (buff_rx[k] != (char)tst1_id)
			{
				throw std::runtime_error(std::string("tst1 failed\n"));
			}
		}

		auto time2 = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time2 - tst1_tim1);;
		if ((ms.count() > tst1_ms0 + 1000) || (tst1_id >= tst1_count)) {
			tst1_ms0 = ms.count();
			std::cerr << "Client [" << native_handle() << "]" << (tst1_id>>10) << "KB/" << (tst1_count>>10)
					<< "KB  (" << (int64_t)tst1_id * 100 / tst1_count << "%)  "
					<< tst1_id / (tst1_ms0 * 1024 * 1024 / 1000) << "MB/s\n";
		}

		if(tst1_id >= tst1_count){
			tst1_id = 0;
			tst1_count = 0;
			std::cerr << " Done\n" << std::endl;
		}
	}


	bool on_request(){
		using namespace cross;
		EResult err;
		char buff_tx[1024];
		char buff_rx[1024];
		int len = 0;

		if(tst1_count > 0){
			on_tst1();
			return false;
		}

		if(read(buff_rx, 4, &len))
			return false;

		if (len == 4 && strncmp(buff_rx, "tst1", 4) == 0) {
			//tst1 test
			//server_tst1();
			if(read(&tst1_count, sizeof(tst1_count)))
				return false;
			std::cout << "Client [" << native_handle() << "] tst1 on " << tst1_count << " bytes:" << std::endl;
			tst1_id = 0;
			return false;
		}
		if (len == 4 && strncmp(buff_rx, "exit", 4) == 0) {
			//tst1 test
			return true;
		}

		//async mode will return with 0 bytes if no other data in the pipe right now
		int len2 = 0;
		if(read(buff_rx + len, sizeof(buff_rx) - len, &len2))
			return false;
		len += len2;

		//response
		//async_write(buff_tx, 10, std::bind(&ipc_server1::on_write, this, _1, _2));
		std::cout << "Client [" << native_handle() << "] got " << len << " bytes:";

		for (int i = 0; i < std::min<>(32, len); i++) {
			printf("%c", buff_rx[i]);
		}

		if(read(buff_rx, 1))
			return false;

		buff_rx[1] = 0;
		printf(" And %s:", buff_rx);
		printf("\n");

		string_copy(buff_tx, "GOT IT");
		err = write(buff_tx, strlen(buff_tx));
		if(err){
			std::cerr << err.message() << std::endl;
		}
		return false;
	}
};





static cross::EResult server(int argc, char * argv[])
{
	using namespace cross;

	EResult err;

	std::atomic<bool> b_exit(false);
	auto		poller = std::make_shared<ipc_connection_poller>();
	auto		listener = std::make_shared<ipc_conn>(poller.get());

	if(!listener){
		return anERROR(-1) << "Cannot create server at [" << servername << "]";
	}

	err = listener->listen(servername);

	if(err){
		std::cerr << err.message() << std::endl;
		return err;
	}

	std::cout << "Waitting..." << std::endl;


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

		if(evt.pconn == listener.get() && evt.e == ipc_poll_event::event::POLLIN){
			//acceptor
			server_conn * pconn = new server_conn(poller.get());

			pconn->accept(listener.get());

			//pconn is automatically added to poller
			std::cout << "Client ["<< pconn->native_handle() << "] connected.\n";
			continue;
		}

		//client connections
		switch(evt.e)
		{
			case ipc_poll_event::event::POLLIN:
			{
				server_conn * pserver_conn = (server_conn *)evt.pconn;
				if(pserver_conn->on_request())
					b_exit.store(true);
			}
			break;
			case ipc_poll_event::event::POLLHUP:
			{
				std::cout << "Client [" << evt.pconn->native_handle() << "] closed and deleted\n";

				//note this is pure virtual base pointer, the actual server will be de-structed and deleted.
				delete evt.pconn;

				break;
			}
			break;
		}
	}

	printf("Over\n");
	return 0;
}


int main(int argc, char * argv[])
{
	//start server in backgound task
	cross::EResult err = cross::daemonize();

	if(err){
		std::cout<<"error:" << err.message() << std::endl;
		return err;
	}else{
		int pid = cross::getPID();
		std::cout<<"daemon: pid=" << pid << std::endl;
	}

	server(argc, argv);

	return 0;
}

