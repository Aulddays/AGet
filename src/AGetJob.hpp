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

	int onTaskDone(AGet::BaseTask *task, CURLcode code);

private:
	struct Task: public AGet::BaseTask
	{
		int id;
		CURL *curl;
		size_t size;
		Task(AGetJob *job, int id, CURL *curl) : AGet::BaseTask(job), id(id), curl(curl), size(0){}
	};
	static size_t onData(char *cont, size_t size, size_t nmemb, Task *task);
	static size_t onHeader(char *cont, size_t size, size_t nmemb, Task *task);
	static int onDebug(CURL *handle, curl_infotype type, char *cont, size_t size, Task *task);

	AGet *aget;
	std::set<Task *> tasks;
};

