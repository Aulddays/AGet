#pragma once

#include <curl/curl.h>

#include "asio.hpp"
#include "AGetJob.hpp"

class AGet
{
public:
	AGet():timer(io_service){};
	~AGet(){};

	int run(const char *url);

private:
	asio::io_service io_service;
	asio::deadline_timer timer;
};
