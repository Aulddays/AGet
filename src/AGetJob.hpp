#pragma once

#include <curl/curl.h>

class AGetJob
{
public:
	AGetJob();
	~AGetJob();

	int run(const char *url);

private:
	struct Task
	{
		AGetJob *job;
		int id;
		CURL *curl;
		size_t size;
		Task(AGetJob *job, int id) : job(job), id(id), curl(NULL), size(0){}
	};
	static size_t onData(void *contents, size_t size, size_t nmemb, void *userp)
	{
		return ((Task *)userp)->job->onData((Task *)userp, contents, size, nmemb);
	}
	size_t onData(Task *task, void *contents, size_t size, size_t nmemb);
	static size_t onHeader(void *contents, size_t size, size_t nmemb, void *userp)
	{
		return ((Task *)userp)->job->onHeader((Task *)userp, contents, size, nmemb);
	}
	size_t onHeader(Task *task, void *contents, size_t size, size_t nmemb);
	static int onDebug(CURL *handle, curl_infotype type, char *contents, size_t size, void *userp)
	{
		return ((Task *)userp)->job->onDebug((Task *)userp, type, contents, size);
	}
	int onDebug(Task *task, curl_infotype type, char *contents, size_t size);
};

