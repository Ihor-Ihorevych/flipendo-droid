#include "Test.h"
#include <cstdio>
#include <exception>

// FlipendoTests [filter]: runs every test (or those whose name contains filter) and returns 1 if any failed.

namespace Tests
{
	namespace
	{
		struct Failure : std::exception
		{
			std::string Message;
			explicit Failure(std::string message) : Message(std::move(message)) {}
		};
	}

	std::vector<Case>& Registry()
	{
		static std::vector<Case> cases;
		return cases;
	}

	void Fail(const char* file, int line, const std::string& what)
	{
		throw Failure(std::string(file) + ":" + std::to_string(line) + ": " + what);
	}

	std::string Describe(double value)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%.9g", value);
		return buf;
	}
}

int main(int argc, char** argv)
{
	std::string filter = argc > 1 ? argv[1] : "";
	int run = 0, failed = 0;
	for (const Tests::Case& test : Tests::Registry())
	{
		if (!filter.empty() && std::string(test.Name).find(filter) == std::string::npos)
			continue;
		run++;
		try
		{
			test.Run();
		}
		catch (const Tests::Failure& failure)
		{
			failed++;
			printf("FAIL %s\n  %s\n", test.Name, failure.Message.c_str());
			continue;
		}
		catch (const std::exception& e)
		{
			failed++;
			printf("FAIL %s\n  %s: exception: %s\n", test.Name, test.File, e.what());
			continue;
		}
		printf("ok   %s\n", test.Name);
	}
	printf("%d tests, %d failed\n", run, failed);
	return failed || run == 0 ? 1 : 0;
}
