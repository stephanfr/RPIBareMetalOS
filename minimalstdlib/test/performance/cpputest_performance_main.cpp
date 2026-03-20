// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <CppUTest/CommandLineTestRunner.h>

#include "../shared/os_abstractions.h"

int main(int argc, char **argv)
{
	minstd::pmr::test::os_abstractions::install_test_cpu_id_provider();
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
