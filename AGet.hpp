#pragma once

#include <set>
#include <curl/curl.h>

class AGetJob;
class AGet
{
public:
	AGet():mode(MODE_ONCE), curlm(NULL), hbtimer(io_service), timer(io_service){};
	~AGet(){ if(curlm) curl_multi_cleanup(curlm); };

	int init();
	int get(const char *url);
	int run(){ mode = MODE_DAEMON;  asio::error_code ec; io_service.run(ec); return ec ? -1 : 0; }
	int runone(){ asio::error_code ec; io_service.run(ec); return ec ? -1 : 0; }

	struct BaseTask
	{
		AGet *aget;
		AGetJob *job;
		int id;
		BaseTask(AGet *aget, AGetJob *job, int id) : aget(aget), job(job), id(id){}
	};

	int addTask(CURL *curl, BaseTask *task);
	int onJobDone(AGetJob *job);

private:
	void onHeartbeat(const asio::error_code & ec);
	// libcurl socket event waiting
	static int doSock(CURL *curl, curl_socket_t sock, int what, AGet *pthis, int *status);
	void onSockEvent(asio::ip::tcp::socket *sock, int action, const asio::error_code & ec, int *status);
	enum	// bit masks for socket watching status
	{
		NEED_READ = 1,
		NEED_WRITE = NEED_READ << 1,
		NEED_MASK = NEED_READ | NEED_WRITE,
		DOING_READ = NEED_WRITE << 1,
		DOING_WRITE = DOING_READ << 1,
		DOING_MASK = DOING_READ | DOING_READ,
		NEED_TO_DOING = 2	// NEED_XXX << 2 == DOING_XXX
	};
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
	enum
	{
		MODE_ONCE,
		MODE_DAEMON
	} mode;
	CURLM *curlm;
	asio::deadline_timer hbtimer;	// timer for heartbeat
	asio::deadline_timer timer;	// timer for libcurl
	std::map<CURL *, BaseTask *> curl2task;	// curl easy handler to task pointer
	std::map<curl_socket_t, asio::ip::tcp::socket *> sockmap;

	std::set<AGetJob *> jobs;
};
