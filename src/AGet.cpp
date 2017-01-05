#include "stdafx.h"

#include <boost/bind.hpp>

#include "AGet.hpp"
#include "AGetJob.hpp"

static class CurlGlobalHelper	// a helper to handle curl global init and cleanup
{
public:
	CurlGlobalHelper::CurlGlobalHelper(){ curl_global_init(CURL_GLOBAL_ALL); }
	CurlGlobalHelper::~CurlGlobalHelper(){ curl_global_cleanup(); }
} curlGlobalHelper;

int AGet::get(const char *url)
{
	AGetJob job(this);
	return job.get(url);
}

int AGet::init()
{
	curlm = curl_multi_init();

	curl_multi_setopt(curlm, CURLMOPT_SOCKETFUNCTION, doSock);
	curl_multi_setopt(curlm, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlm, CURLMOPT_TIMERFUNCTION, doTimer);
	curl_multi_setopt(curlm, CURLMOPT_TIMERDATA, this);

	return 0;
}

int AGet::addTask(BaseTask *task)
{
	return 0;
}

// CURLMOPT_SOCKETFUNCTION
int AGet::doSock(CURL *curl, curl_socket_t csock, int what, AGet *pthis, void *)
{
	fprintf(stderr, "dosock: socket=%d, what=%d\n", csock, what);

	asio::ip::tcp::socket * sock = NULL;
	{
		auto isock = pthis->sockmap.find(csock);
		if (isock == pthis->sockmap.end())
		{
			fprintf(stderr, "socket %d is a c-ares socket, ignoring\n", csock);
			return 0;
		}
		sock = isock->second;
	}

	switch (what)
	{
	case CURL_POLL_REMOVE:
		fprintf(stderr, "remove sock\n");
		// TODO: confirm nothing to do?
		break;
	case CURL_POLL_IN:
			fprintf(stderr, "watching for socket to become readable\n");
			sock->async_read_some(asio::null_buffers(),
				boost::bind(&onSockEvent, pthis, sock, what));
			break;
	case CURL_POLL_OUT:
		fprintf(stderr, "watching for socket to become readable\n");
		sock->async_write_some(asio::null_buffers(),
			boost::bind(&onSockEvent, pthis, sock, what));
		break;
	case CURL_POLL_INOUT:
		fprintf(stderr, "watching for socket to become readable & writable\n");
		sock->async_read_some(asio::null_buffers(),
			boost::bind(&onSockEvent, pthis, sock, what));
		sock->async_write_some(asio::null_buffers(),
			boost::bind(&onSockEvent, pthis, sock, what));
		break;
	default:
		fprintf(stderr, "Unsupported action %d\n", what);
		break;
	}

	return 0;
}

// async event callback from doSock()
void AGet::onSockEvent(asio::ip::tcp::socket *sock, int action)
{
	fprintf(stderr, "onSockEvent: action=%d", action);

	CURLMcode rc;
	int running = 0;
	rc = curl_multi_socket_action(curlm, sock->native_handle(), action, &running);
	if (rc != CURLM_OK)
		fprintf(stderr, "Fatal error of libcurl %d\n", (int)rc);

	checkTasks();

	if (running <= 0)
	{
		fprintf(stderr, "last transfer done, kill timeout\n");
		timer.cancel();
	}
}

// CURLMOPT_TIMERFUNCTION: Update the event timer after curl_multi library calls
int AGet::doTimer(CURLM *curlm, long timeout_ms, AGet *pthis)
{
	fprintf(stderr, "doTimer: timeout_ms %ld", timeout_ms);

	// cancel running timer
	pthis->timer.cancel();

	if (timeout_ms > 0)
	{
		// update timer
		pthis->timer.expires_from_now(boost::posix_time::millisec(timeout_ms));
		pthis->timer.async_wait(boost::bind(&onTimer,pthis, _1));
	}
	else
	{
		// call timeout function immediately
		asio::error_code ec;
		pthis->onTimer(ec);
	}

	return 0;
}

// timeup callback from doTimer()
void AGet::onTimer(const asio::error_code & ec)
{
	if (!ec)
	{
		fprintf(stderr, "onTimer\n");

		CURLMcode rc;
		int running = 0;
		rc = curl_multi_socket_action(curlm, CURL_SOCKET_TIMEOUT, 0, &running);

		if (rc != CURLM_OK)
			fprintf(stderr, "Fatal error of libcurl %d\n", (int)rc);

		checkTasks();
	}
}

/* CURLOPT_OPENSOCKETFUNCTION */
curl_socket_t AGet::openSock(AGet *pthis, curlsocktype purpose, curl_sockaddr *address)
{
	fprintf(stderr, "openSock\n");

	curl_socket_t sockfd = CURL_SOCKET_BAD;

	// currently only support IPv4
	if (purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET)
	{
		// create an asio socket object
		asio::ip::tcp::socket *sock = new asio::ip::tcp::socket(pthis->io_service);

		// open it and get the native handle
		asio::error_code ec;
		sock->open(asio::ip::tcp::v4(), ec);
		if (ec)
			fprintf(stderr, "Open socket failed %s\n", ec.message().c_str());
		else
		{
			sockfd = sock->native_handle();
			fprintf(stderr, "Opened socket %d\n", (int)sockfd);
			// save it for monitoring
			pthis->sockmap.insert(std::pair<curl_socket_t, asio::ip::tcp::socket *>(sockfd, sock));
		}
	}
	else
		fprintf(stderr, "Unsupported purpose %d or family %d\n", (int)purpose, address->family);

	return sockfd;
}

// CURLOPT_CLOSESOCKETFUNCTION
int AGet::closeSock(AGet *pthis, curl_socket_t item)
{
	fprintf(stderr, "close_socket : %d\n", (int)item);

	std::map<curl_socket_t, asio::ip::tcp::socket *>::iterator it = pthis->sockmap.find(item);

	if (it != pthis->sockmap.end())
	{
		delete it->second;
		pthis->sockmap.erase(it);
	}

	return 0;
}

// check whether any task was done
void AGet::checkTasks()
{
	int msgs_left;
	while (CURLMsg *msg = curl_multi_info_read(curlm, &msgs_left))
	{
		if (msg->msg == CURLMSG_DONE)
		{
			CURL *curl = msg->easy_handle;
			curl_multi_remove_handle(curlm, curl);
			onTaskDone(curl, msg->data.result);
		}
	}
}

int AGet::onTaskDone(CURL *curl, CURLcode code)
{
	std::map<CURL *, BaseTask *>::iterator itask = curl2task.find(curl);
	if (itask == curl2task.end())
	{
		fprintf(stderr, "Invalid curl handle.\n");
		return -1;
	}
	BaseTask *task = itask->second;
	return task->job->onTaskDone(task, code);
}