#include "stdafx.h"

#include "AGet.hpp"

int main(int, char **, char **)
{
	AGet aget;
	aget.init();

	if (aget.get("http://www.sohu.com/"))
		return -1;

	return aget.run();
}
