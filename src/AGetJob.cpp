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
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, onProgress);
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
	AGetJob *pthis = task->job;
	// check response code
	if (pthis->status == JOB_START)	// initial task
	{
		assert(task->id == 0);
	}
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

// paser httpheader (in the form "Key: Value") line, fill Key in k, Value in v, with space trimmed
static int parseHttpHeader(const char *header, size_t hlen, char *k, size_t klen, char *v, size_t vlen)
{
	const char *hend = header + hlen;
	char *buf[] = {k, v};
	size_t size[] = { klen - 1, vlen - 1 };
	char del[] = { ':', '\0' };
	if (klen <= 0 || vlen <= 0)
		return -1;
	for (size_t i = 0; i < sizeof(buf) / sizeof(buf[0]); ++i)
	{
		while (header < hend && *header > 0 && isspace(*header))
			++header;
		char *lastsp = NULL;
		for (; header < hend && *header != del[i]; ++header)
		{
			bool space = *header > 0 && isspace(*header);
			if (size[i] <= 0 && !space)
				lastsp = NULL;
			if (size[i] <= 0)
				continue;
			if (!space)
				lastsp = NULL;
			else if (!lastsp)
				lastsp = buf[i];
			*buf[i]++ = *header;
			--size[i];
		}
		if (lastsp)
			*lastsp = 0;
		else
			*buf[i] = 0;
		if (header < hend)
			++header;
	}
	return 0;
}

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
		{
			task->clear();
			task->status = Task::TASK_START;
		}
		if (task->status != Task::TASK_START)
			break;	// Ignore any header(-like) line after get data
		const size_t HKVBLEN = 31;
		char key[HKVBLEN], value[HKVBLEN];
		parseHttpHeader(cont, size, key, HKVBLEN, value, HKVBLEN);
		//fprintf(stderr, "parseHttpHeader() key %s, value %s.\n", key, value);
		if ((strcasecmp(key, "Content-Encoding") == 0 || strcasecmp(key, "Transfer-Encoding") == 0) &&
			!(!*value || strcasecmp(value, "identity")))
		{
			task->encode = true;
		}
		else if (strcasecmp(key, "Content-Length") == 0)
		{
			char *endptr = NULL;
			unsigned long long ct = strtoull(value, &endptr, 10);
			if (!endptr && ct != ULLONG_MAX)
				task->size = (uint64_t)ct;
		}
		else if (*cont == '\r' || *cont == '\n' || !*cont)
			task->status = Task::TASK_GOTHEADER;
		break;
	}
	}

	return 0;
}
