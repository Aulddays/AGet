#include "stdafx.h"
#include "AGetJob.hpp"


AGetJob::AGetJob(AGet *aget) : aget(aget), size(-1), status(JOB_START)
{
}


AGetJob::~AGetJob()
{
}

int AGetJob::get(const char *geturl)
{
	url = geturl;
	CURL *curl = curl_easy_init();
	if (!curl)
	{
		fprintf(stderr, "curl_easy_init() failed\n");
		return -1;
	}

	Task *task = new Task(aget, this, 0, curl, 0, -1);
	tasks.insert(task);

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)task);
	//curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
	//curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)task);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, onDebug);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)task);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
	//curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);

	return aget->addTask(curl, task);
}

int AGetJob::onTaskDone(AGet::BaseTask *basetask, CURLcode code)
{
	Task *task = (Task *)basetask;
	if (tasks.find(task) == tasks.end())
	{
		fprintf(stderr, "Invalid task.\n");
		return -1;
	}
	if (code != CURLE_OK) {
		fprintf(stderr, "task failed: %s\n", curl_easy_strerror(code));
	}
	else {
		printf("%lu bytes retrieved in total\n", (unsigned long)task->got);
	}

	tasks.erase(task);
	curl_easy_cleanup(task->curl);
	delete task;

	return aget->onJobDone(this);
}

size_t AGetJob::onData(char *cont, size_t size, size_t nmemb, Task *task)
{
	size_t realsize = size * nmemb;
	printf("%lu bytes retrieved\n", (unsigned long)realsize);
	task->got += realsize;

	//FILE *fp = fopen("out.htm", task->size == realsize ? "wb" : "ab");
	//if (fp)
	//{
	//	fwrite(cont, realsize, 1, fp);
	//	fclose(fp);
	//}

	return realsize;
}

//size_t AGetJob::onHeader(char *cont, size_t size, size_t nmemb, Task *task)
//{
//	size_t realsize = size * nmemb;
//	fputs("< ", stderr);
//	fwrite(cont, size, nmemb, stderr);
//
//	return realsize;
//}

int AGetJob::onDebug(CURL *handle, curl_infotype type, char *cont, size_t size, Task *task)
{
	switch (type)
	{
	case CURLINFO_HEADER_OUT:
	{
		// seems that the entire out headers come at once
		while (char *pend = (char *)memchr((void *)cont, '\n', size))
		{
			fputs("> ", stderr);
			fwrite(cont, pend - cont + 1, 1, stderr);
			size -= pend - cont + 1;
			cont = pend + 1;
		}
		break;
	}
	case CURLINFO_HEADER_IN:
	{
		fputs("< ", stderr);
		fwrite(cont, size, 1, stderr);
		if (task->status == Task::TASK_GOTHEADER)
			task->clear();
		break;
	}
	}

	return 0;
}
