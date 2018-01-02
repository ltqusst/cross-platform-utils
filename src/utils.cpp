#include "utils.h"

namespace cross {

int getPID()
{
#ifdef linux
	return getpid();
#endif

#ifdef WIN32
	return ::GetCurrentProcessId();
#endif
}

EResult daemonize(void)
{
#ifdef linux
	int nochdir = 1;
	int noclose = 1;
	if (daemon(nochdir, noclose) < 0)
	{
		return anERROR(-1, errno) << "daemon() failed";
	}
#endif

#ifdef WIN32
	//instead of daemonize, win32 support create background subprocess
	//directly, so 

	if (GetStdHandle(STD_INPUT_HANDLE) == INVALID_HANDLE_VALUE) {
		//we are daemon already
		//return anERROR(-2) << "no STD_INPUT_HANDLE";
		return 0;
	}

	//FreeConsole()
	// Start the child process.
	std::string cur_exe_path(1024, 0);

	if (!GetModuleFileName(NULL,
		(LPTSTR)cur_exe_path.data(),
		(DWORD)(cur_exe_path.capacity() - 1)))
		return anERROR(-1, ::GetLastError()) << "GetModuleFileName failed.";

	std::cerr << cur_exe_path << std::endl;

	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (!CreateProcess(cur_exe_path.c_str(),   // No module name (use command line)
		NULL,        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		DETACHED_PROCESS,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		return anERROR(-2, ::GetLastError()) << "CreateProcess failed.";
	}
	else {
		//now we exit and let daemon do the job
		std::cerr << "daemon processID=" << pi.dwProcessId << std::endl;

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		std::exit(0);
	}
#endif

	return 0;
}

EResult create_daemon(char* argv[])
{
	if (argv[0] == NULL)
		return anERROR(-1) << "create_daemon() with NULL argv[0]";
#ifdef linux
	int stat = 0;
	pid_t pid;

	std::string path(argv[0]);

	if (path.empty()) 
		return anERROR(-1) << "Error: Need specify path.";
	
	if (access(path.c_str(), F_OK) < 0) 
		return anERROR(-1) << "Error: Cannot find executable:" << path ;
	
	if (access(hddlService.c_str(), X_OK) < 0) 
		return anERROR(-1) << "Error: Target file has no execute permission:" << path;
	
	pid = fork();
	if (pid < 0) 
		return anERROR(-1, errno) << "Error: Fork failed";
	
	if (pid > 0) {
		// Parent process.
		//HInfo("Info: HDDL Service is forked, pid = %d.", pid);
		//this wait will return as soon as child process is daemonized
		waitpid(pid, &stat, 0);
		return pid;
	}

	// Child Process
	//char* const argv[] = { const_cast<char* const>(hddlService.c_str()),
	//	const_cast<char* const>(hddlInstallDir.c_str()),
	//	const_cast<char* const>((char*)NULL) };

	if (execv(argv[0], argv))
		return anERROR(-1,errno) << "Error: execv %s failed";

	return 0;
#endif

#ifdef WIN32
	std::stringstream ss;
	
	ss << argv[0];
	for (int i = 1; argv[i] != NULL; i++) {
		ss << " " << argv[i];
	}
	
	std::string cmdline = ss.str();
	
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (!CreateProcess(NULL,   // No module name (use command line)
		(LPTSTR)cmdline.data(),			// Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		DETACHED_PROCESS,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		return anERROR(-1, ::GetLastError()) << "CreateProcess failed.";
	}
	else {
		//now we exit and let daemon do the job
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	return 0;
#endif
}


}
