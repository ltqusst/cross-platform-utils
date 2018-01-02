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

	cross::EResult on_tst1(){
		char buff_rx[1024];
		int len = sizeof(buff_rx);

		if (tst1_id == 0) {
			tst1_tim1 = std::chrono::steady_clock::now();
			tst1_ms0 = 0;
		}
		
		if (len > tst1_count - tst1_id) len = tst1_count - tst1_id;

		auto err = read(buff_rx, len);
		if (err) {
			return anERROR(-1) << "on_tst1() read failed" << err;
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
		return 0;
	}

	class echo {
		int				eh_len		= 0;
		int				eh_progress = 0;
		char			eh_buff[1024];
	public:
		bool is_active(){	return eh_len > 0;	}
		int size() { return eh_len; }
		cross::EResult start(server_conn * pconn)
		{
			//init
			int len = 0;

			eh_len = 0;
			eh_progress = 0;

			if (pconn->read(&len, sizeof(eh_len))) 
				return anERROR(-1) << "read echo length failed";
			
			if (len > sizeof(eh_buff))
				return anERROR(-1) << "eh_buff[" << sizeof(eh_buff) << "] is not big enough for echo of size=" << len;
			
			eh_len = len;	//assign here is fail-safe
			std::cout << "Client [" << pconn->native_handle() << "] echo on " << size() << " bytes:" << std::endl;

			return 0;
		}
		cross::EResult on_echo(server_conn * pconn)
		{
			cross::EResult err;
			int cnt = 0;

			//make sure in any case
			auto g1 = cross::make_scopeGuard([&] {eh_len = 0; });

			//non-blocking version
			err = pconn->read(eh_buff + eh_progress, eh_len - eh_progress, &cnt);
			if (err) return err;

			eh_progress += cnt;

			if (eh_progress > eh_len)
				return anERROR(-1) << "eh_progress=" << eh_progress << " is bigger than expected " << eh_len;

			if (eh_progress < eh_len){
				g1.dismiss();
			}
			else
			{
				//echo back
				err = pconn->write(eh_buff, eh_len);
				if (err) return err;
				std::cout << "Client [" << pconn->native_handle() << "] echo-ed back." << std::endl;
			}
			return 0;
		}
	} m_echo;

	bool on_request(){
		using namespace cross;
		EResult err;
		char buff_tx[1024];
		char buff_rx[1024];
		int len = 0;

		if(tst1_count > 0){
			err = on_tst1();
			if (err) {
				std::cerr << err.message() << std::endl;
			}
			return false;
		}

		if (m_echo.is_active()) {
			err = m_echo.on_echo(this);
			if (err) 
				std::cerr << err.message() << std::endl;
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
		if (len == 4 && strncmp(buff_rx, "echo", 4) == 0) {
			err = m_echo.start(this);
			if (err)
				std::cerr << err.message() << std::endl;				
			return false;
		}

		//async mode will return with 0 bytes if no other data in the pipe right now
		int len2 = 0;
		if(read(buff_rx + len, sizeof(buff_rx) - len, &len2))
			return false;
		len += len2;

		//response
		//async_write(buff_tx, 10, std::bind(&ipc_server1::on_write, this, _1, _2));
		std::cout << "Client [" << native_handle() << "] got " << len << " bytes:";

		for (int i = 0; i < len; i++)
			if (i > 32) {
				std::cout << "...";
				break;
			}else
				std::cout << buff_rx[i];

		std::cout << std::flush;

		if(read(buff_rx, 1))
			return false;

		buff_rx[1] = 0;
		std::cout << " And " << buff_rx << std::endl;

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
	//"-d" invoke from console/terminal but start daemon background
	if (argc > 1 && strcmp(argv[1],"-d") == 0) {
		//start server in backgound task
		std::cout << "before: pid=" << cross::getPID() << std::endl;

		cross::EResult err = cross::daemonize();

		if (err) {
			std::cout << "error:" << err.message() << std::endl;
			return err;
		}
		else {
			std::cout << "daemonized: pid=" << cross::getPID() << std::endl;
		}
	}

	server(argc, argv);

	return 0;
}

