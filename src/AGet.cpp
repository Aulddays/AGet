#include "stdafx.h"

#include <boost/bind.hpp>

#include "AGet.hpp"
#include "AGetJob.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

static class CurlGlobalHelper	// a helper to handle curl global init and cleanup
{
public:
	CurlGlobalHelper::CurlGlobalHelper(){ curl_global_init(CURL_GLOBAL_ALL); }
	CurlGlobalHelper::~CurlGlobalHelper(){ curl_global_cleanup(); }
} curlGlobalHelper;

int AGet::init()
{
	curlm = curl_multi_init();

	curl_multi_setopt(curlm, CURLMOPT_SOCKETFUNCTION, doSock);
	curl_multi_setopt(curlm, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlm, CURLMOPT_TIMERFUNCTION, doTimer);
	curl_multi_setopt(curlm, CURLMOPT_TIMERDATA, this);

	hbtimer.expires_from_now(boost::posix_time::millisec(1000));
	hbtimer.async_wait(boost::bind(&AGet::onHeartbeat, this, _1));

	return 0;
}

int AGet::get(const char *url)
{
	AGetJob *job = new AGetJob(this);
	jobs.insert(job);
	int ret = job->get(url);
	if (ret)
	{
		fprintf(stderr, "Start job failed.\n");
		jobs.erase(job);
	}
	return ret;
}

int AGet::onJobDone(AGetJob *job)
{
	std::set<AGetJob *>::iterator ijob = jobs.find(job);
	if (ijob == jobs.end())
	{
		fprintf(stderr, "Invalid job object.\n");
		return -1;
	}
	fprintf(stderr, "Job done.\n");
	jobs.erase(ijob);
	delete job;
	if (jobs.empty() && mode == MODE_ONCE)
		hbtimer.cancel();
	return 0;
}

int AGet::addTask(CURL *curl, BaseTask *task)
{
	if (curl2task.find(curl) != curl2task.end())
	{
		fprintf(stderr, "task already running.\n");
		return -1;
	}
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, openSock);
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, (void *)this);
	curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, closeSock);
	curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, (void *)this);
	//curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 1L);
	//curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 10L);
	CURLMcode rc = curl_multi_add_handle(curlm, curl);
	if (rc != CURLM_OK)
	{
		fprintf(stderr, "curl_multi_add_handle() failed %d.\n", (int)rc);
		return -1;
	}
	curl2task[curl] = task;
	return 0;
}

int AGet::onTaskDone(CURL *curl, CURLcode code)
{
	std::map<CURL *, BaseTask *>::iterator itask = curl2task.find(curl);
	if (itask == curl2task.end())
	{
		fprintf(stderr, "Invalid curl handle.\n");
		return -1;
	}
	curl_multi_remove_handle(curlm, curl);
	BaseTask *task = itask->second;
	return task->job->onTaskDone(task, code);
}

void AGet::onHeartbeat(const asio::error_code & ec)
{
	if (!ec)
	{
		time_t now = time(NULL);
		for (auto i = jobs.begin(); i != jobs.end();)
		{
			auto next = i;
			++next;	// i may be invalidated during heartBeat, so must not ++i directly
			(*i)->onHeartbeat(now);
			i = next;
		}
		hbtimer.expires_from_now(boost::posix_time::millisec(1000));
		hbtimer.async_wait(boost::bind(&AGet::onHeartbeat, this, _1));
	}
	else
		PELOG_LOG((PLV_INFO, "hbtimer %s\n", ec.message().c_str()));
}

////////// libcurl-asio helpers
// CURLMOPT_SOCKETFUNCTION
int AGet::doSock(CURL *curl, curl_socket_t csock, int what, AGet *pthis, int *status)
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

	// print logs
	switch (what)
	{
	case CURL_POLL_REMOVE:
		fprintf(stderr, "remove sock\n");
		break;
	case CURL_POLL_IN:
		fprintf(stderr, "watching for socket to become readable\n");
		break;
	case CURL_POLL_OUT:
		fprintf(stderr, "watching for socket to become writable\n");
		break;
	case CURL_POLL_INOUT:
		fprintf(stderr, "watching for socket to become readable & writable\n");
		break;
	default:
		fprintf(stderr, "Unsupported action %d\n", what);
		break;
	}

	// calculate need
	int need = 0;
	if (what == CURL_POLL_IN || what == CURL_POLL_INOUT)
		need |= NEED_READ;
	if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT)
		need |= NEED_WRITE;
	// manage memory for status
	if (need && !status)
	{
		status = new int(need);
		curl_multi_assign(pthis->curlm, csock, status);
	}
	else if (!need && status)	// already watching now and remove is requested
	{
		// Immediately remove status pointer from socket, incase the socket are to be added elsewhere later.
		// According to asio document, cancel() on async actions is not guarranteed to suceed. So if the
		// socket is being watched on read/write, just keep that on and keep the memory of status, so that
		// when onSockEvent() get called when something happens to the watch, it will know that the watch
		// is no longer needed and will not inform curl with the event.
		curl_multi_assign(pthis->curlm, csock, NULL);
		if (*status == 0)
		{
			delete status;
			status = NULL;
		}
	}
	// update need in status
	if (status)
		*status = need | (*status & ~NEED_MASK);

	if ((need & NEED_READ) && !(*status & DOING_READ))
	{
		sock->async_read_some(asio::null_buffers(), boost::bind(&onSockEvent, pthis, sock, DOING_READ, asio::placeholders::error, status));
		*status |= DOING_READ;
	}
	if ((need & NEED_WRITE) && !(*status & DOING_WRITE))
	{
		sock->async_write_some(asio::null_buffers(), boost::bind(&onSockEvent, pthis, sock, DOING_WRITE, asio::placeholders::error, status));
		*status |= DOING_WRITE;
	}

	return 0;
}

// async event callback from doSock()
void AGet::onSockEvent(asio::ip::tcp::socket *sock, int action, const asio::error_code & ec, int *status)
{
	fprintf(stderr, "onSockEvent: action=%d\n", action);

	if ((action >> NEED_TO_DOING) & *status)	// socket not yet removed
	{
		CURLMcode rc;
		int running = 0;
		int ev_bitmask = ec ? CURL_CSELECT_ERR : (action == DOING_READ ? CURL_CSELECT_IN : CURL_CSELECT_OUT);
		rc = curl_multi_socket_action(curlm, sock->native_handle(), ev_bitmask, &running);
		if (rc != CURLM_OK)
			fprintf(stderr, "Fatal error of libcurl %d\n", (int)rc);

		checkTasks();

		if (running <= 0)
		{
			fprintf(stderr, "last transfer done, kill timeout\n");
			timer.cancel();
		}

		if (!ec && ((action >> NEED_TO_DOING) & *status))	// no error, keep watching
		{
			if (action == DOING_READ)
				sock->async_read_some(asio::null_buffers(), boost::bind(&AGet::onSockEvent, this, sock, DOING_READ, asio::placeholders::error, status));
			else
				sock->async_write_some(asio::null_buffers(), boost::bind(&AGet::onSockEvent, this, sock, DOING_WRITE, asio::placeholders::error, status));
			fprintf(stderr, "watch %s\n", action == DOING_READ ? "read" : "write");
		}
	}
	if (ec || !((action >> NEED_TO_DOING) & *status))
	{
		*status &= ~action;	// clear the DOING bit
		if (*status == 0)	// no need watching and not watching
			delete status;
	}
}

// CURLMOPT_TIMERFUNCTION: Update the event timer after curl_multi library calls
int AGet::doTimer(CURLM *curlm, long timeout_ms, AGet *pthis)
{
	fprintf(stderr, "doTimer: timeout_ms %ld\n", timeout_ms);

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
	fprintf(stderr, "checkTasks\n");
	while (CURLMsg *msg = curl_multi_info_read(curlm, &msgs_left))
	{
		fprintf(stderr, "Task message %d\n", (int)msg->msg);
		if (msg->msg == CURLMSG_DONE)
			onTaskDone(msg->easy_handle, msg->data.result);
	}
}
