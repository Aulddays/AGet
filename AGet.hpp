#pragma once

#include <curl/curl.h>

#include "asio.hpp"

class AGetJob;
class AGet
{
public:
	AGet():curlm(NULL), timer(io_service){};
	~AGet(){ if(curlm) curl_multi_cleanup(curlm); };

	int init();
	int get(const char *url);
	int run(){ asio::error_code ec; io_service.run(ec); return ec ? -1: 0; }

	struct BaseTask
	{
		AGetJob *job;
		BaseTask(AGetJob *job) : job(job){}
	};

	int addTask(BaseTask *task);

private:
	// libcurl socket event waiting
	static int doSock(CURL *curl, curl_socket_t sock, int what, AGet *pthis, void *);
	void onSockEvent(asio::ip::tcp::socket *sock, int action);
	// libcurl timer waiting
	static int doTimer(CURLM *curlm, long timeout_ms, AGet *pthis);
	void onTimer(const asio::error_code & ec);
	// libcurl opensocket and closesocket. handle these to keep record of sockmap
	static curl_socket_t openSock(AGet *pthis, curlsocktype purpose, curl_sockaddr *address);
	static int closeSock(AGet *pthis, curl_socket_t item);
	// check whether any task was done
	void checkTasks();

	int onTaskDone(CURL *curl, CURLcode code);

private:
	asio::io_service io_service;
	CURLM *curlm;
	asio::deadline_timer timer;	// timer for libcurl
	std::map<CURL *, BaseTask *> curl2task;	// curl easy handler to task pointer
	std::map<curl_socket_t, asio::ip::tcp::socket *> sockmap;
};
