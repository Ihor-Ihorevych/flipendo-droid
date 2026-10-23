#pragma once

#include <cmath>
#include <string>
#include <vector>

// A minimal test runner for FlipendoTests (src/tests/tests.cmake): code that needs no game data, linked against
// SurrealCommon like the game. TEST(name) registers a test, CHECK* fail it (the rest of that test is skipped).

namespace Tests
{
	struct Case
	{
		const char* Name;
		const char* File;
		void (*Run)();
	};

	std::vector<Case>& Registry();

	struct Register
	{
		Register(const char* name, const char* file, void (*run)()) { Registry().push_back({ name, file, run }); }
	};

	[[noreturn]] void Fail(const char* file, int line, const std::string& what);
	std::string Describe(double value);
}

#define TEST(name) \
	static void name(); \
	static Tests::Register name##_register(#name, __FILE__, name); \
	static void name()

#define CHECK(cond) \
	do { if (!(cond)) Tests::Fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b) \
	do { \
		auto check_a = (a); auto check_b = (b); \
		if (!(check_a == check_b)) \
			Tests::Fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b "): " + Tests::Describe((double)check_a) + " != " + Tests::Describe((double)check_b)); \
	} while (0)

#define CHECK_NEAR(a, b, eps) \
	do { \
		double check_a = (a), check_b = (b); \
		if (!(std::fabs(check_a - check_b) <= (eps))) \
			Tests::Fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b "): " + Tests::Describe(check_a) + " vs " + Tests::Describe(check_b)); \
	} while (0)
