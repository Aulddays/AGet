#pragma once

#include <curl/curl.h>

#include "AGetJob.hpp"

class AGet
{
public:
	AGet(){};
	~AGet(){};

	int run(const char *url);

private:
	static class CurlGlobalHelper	// a helper to handle curl global init and cleanup
	{
	public:
		CurlGlobalHelper::CurlGlobalHelper(){ curl_global_init(CURL_GLOBAL_ALL); }
		CurlGlobalHelper::~CurlGlobalHelper(){ curl_global_cleanup(); }
	} curlGlobalHelper;
};
