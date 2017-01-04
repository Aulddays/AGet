#include "stdafx.h"

#include "AGet.hpp"

static class CurlGlobalHelper	// a helper to handle curl global init and cleanup
{
public:
	CurlGlobalHelper::CurlGlobalHelper(){ curl_global_init(CURL_GLOBAL_ALL); }
	CurlGlobalHelper::~CurlGlobalHelper(){ curl_global_cleanup(); }
} curlGlobalHelper;

int AGet::run(const char *url)
{
	AGetJob job;
	return job.run(url);
}