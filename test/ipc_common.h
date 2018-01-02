/*
 * ipc_common.h
 *
 *  Created on: Dec 25, 2017
 *      Author: hddls
 */

#ifndef TEST_IPC_COMMON_H_
#define TEST_IPC_COMMON_H_



#ifdef linux
static const char * servername = "/var/tmp/hddl_service.sock";
using ipc_conn = cross::ipc_connection_linux_UDS;
#endif


#ifdef WIN32
static LPTSTR servername = TEXT("\\\\.\\pipe\\mynamedpipe");
using ipc_conn = cross::ipc_connection_win_namedpipe;
#endif


#endif /* TEST_IPC_COMMON_H_ */
