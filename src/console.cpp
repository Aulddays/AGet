#include "stdafx.h"

#include "AGet.hpp"

int main(int, char **, char **)
{
	AGet aget;

	return aget.get("http://www.sohu.com/");
}