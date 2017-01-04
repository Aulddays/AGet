#include "stdafx.h"
#include "AGetJob.hpp"


AGetJob::AGetJob()
{
}


AGetJob::~AGetJob()
{
}

int AGetJob::run(const char *url)
{
	CURLcode res;

	Task task(this, 0);

	task.curl = curl_easy_init();

	curl_easy_setopt(task.curl, CURLOPT_URL, url);

	curl_easy_setopt(task.curl, CURLOPT_WRITEFUNCTION, (size_t (*)(void *, size_t, size_t, void *))onData);
	curl_easy_setopt(task.curl, CURLOPT_WRITEDATA, (void *)&task);
	curl_easy_setopt(task.curl, CURLOPT_HEADERFUNCTION, (size_t(*)(void *, size_t, size_t, void *))onHeader);
	curl_easy_setopt(task.curl, CURLOPT_HEADERDATA, (void *)&task);
	curl_easy_setopt(task.curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(task.curl, CURLOPT_DEBUGFUNCTION, (int(*)(CURL *, curl_infotype, char *, size_t, void *))onDebug);
	curl_easy_setopt(task.curl, CURLOPT_DEBUGDATA, (void *)&task);
	curl_easy_setopt(task.curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(task.curl, CURLOPT_SSL_VERIFYPEER, FALSE);

	res = curl_easy_perform(task.curl);

	if (res != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
			curl_easy_strerror(res));
	}
	else {
		printf("%lu bytes retrieved in total\n", (unsigned long)task.size);
	}

	/* cleanup curl stuff */
	curl_easy_cleanup(task.curl);

	return 0;
}

size_t AGetJob::onData(Task *task, void *contents, size_t size, size_t nmemb)
{
	size_t realsize = size * nmemb;
	printf("%lu bytes retrieved\n", (unsigned long)realsize);
	task->size += realsize;

	return realsize;
}

size_t AGetJob::onHeader(Task *task, void *contents, size_t size, size_t nmemb)
{
	size_t realsize = size * nmemb;
	fwrite(contents, size, nmemb, stderr);

	return realsize;
}

int AGetJob::onDebug(Task *task, curl_infotype type, char *contents, size_t size)
{
	if (type == CURLINFO_HEADER_OUT)
		fwrite(contents, size, 1, stderr);

	return 0;
}
