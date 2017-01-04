#include "stdafx.h"

#include "AGet.hpp"

int AGet::run(const char *url)
{
	AGetJob job;
	return job.run(url);
}