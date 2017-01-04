#include "stdafx.h"

#include "AGet.hpp"

int main(int, char **, char **)
{
	AGet aget;

	return aget.run("http://www.sohu.com/");
}