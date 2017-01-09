#pragma once

#include <set>
#include <curl/curl.h>

#include "AGet.hpp"

class AGetJob
{
public:
	AGetJob(AGet *aget);
	~AGetJob();

	int get(const char *url);

	int onTaskDone(AGet::BaseTask *basetask, CURLcode code);

private:
	struct Task: public AGet::BaseTask
	{
		CURL *curl;
		uint64_t start;
		uint64_t end;
		uint64_t got;
		uint64_t written;
		uint64_t size;	// Content-Size
		bool encode;	// compressed or chunked
		enum
		{
			TASK_START,
			TASK_GOTHEADER,	// finished header
			TASK_GOTDATA,	// got some data
		} status;
		Task(AGet *aget, AGetJob *job, int id, CURL *curl, uint64_t start, uint64_t end) :
			AGet::BaseTask(aget, job, id), curl(curl), start(start), end(end), got(0), written(0), status(TASK_START){ clear(); }
		void clear()
		{
			size = -1;
			encode = false;
		}
	};
	static size_t onData(char *cont, size_t size, size_t nmemb, Task *task);
	//static size_t onHeader(char *cont, size_t size, size_t nmemb, Task *task);
	static int onDebug(CURL *handle, curl_infotype type, char *cont, size_t size, Task *task);

	AGet *aget;
	std::string url;
	std::set<Task *> tasks;
	uint64_t size;

	enum
	{
		JOB_START,	// waiting for more information
		JOB_SINGLE,	// force single task mode, no content-length or compressed
		JOB_MULTI,	// multi task mode. May be running only 1 task because of small size or user setting
		JOB_DONE	// finished
	} status;
};

