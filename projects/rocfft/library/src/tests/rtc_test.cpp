// Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "../../../shared/environment.h"
#include "rtc_cache.h"
#include <gtest/gtest.h>

#if __has_include(<filesystem>)
#include <filesystem>
#else
#include <experimental/filesystem>
namespace std
{
    namespace filesystem = experimental::filesystem;
}
#endif

#ifndef _WIN32
#include <errno.h> // program_invocation_name
#endif

namespace fs = std::filesystem;

static const char* simple_kernel_jit = R"(
extern "C" __global__ void simple_kernel(int* input)
{
    *input = 1337;
}
)";

TEST(rocfft_internal, rtc_cache_arch_candidates_exact_first)
{
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1030"),
              (std::vector<std::string>{"gfx1030", "gfx10-3-generic"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1036"),
              (std::vector<std::string>{"gfx1036", "gfx10-3-generic"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1200"),
              (std::vector<std::string>{"gfx1200", "gfx12-generic"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1201"),
              (std::vector<std::string>{"gfx1201", "gfx12-generic"}));

    // Already-generic and unrelated architectures are not expanded.
    EXPECT_EQ(rtc_cache_arch_candidates("gfx10-3-generic"),
              (std::vector<std::string>{"gfx10-3-generic"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx12-generic"),
              (std::vector<std::string>{"gfx12-generic"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1100"),
              (std::vector<std::string>{"gfx1100"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1037"),
              (std::vector<std::string>{"gfx1037"}));
    EXPECT_EQ(rtc_cache_arch_candidates("gfx1202"),
              (std::vector<std::string>{"gfx1202"}));

    // A generic AOT build includes tuned kernels for every family member.
    EXPECT_TRUE(rtc_cache_arch_matches_solution("gfx10-3-generic", "gfx1030"));
    EXPECT_TRUE(rtc_cache_arch_matches_solution("gfx10-3-generic", "gfx1036"));
    EXPECT_TRUE(rtc_cache_arch_matches_solution("gfx12-generic", "gfx1200"));
    EXPECT_TRUE(rtc_cache_arch_matches_solution("gfx12-generic", "gfx1201"));
    EXPECT_FALSE(rtc_cache_arch_matches_solution("gfx12-generic", "gfx1100"));
}

TEST(rocfft_internal, rtc_cache_generic_db_key_fallback)
{
    const std::array<char, 32> generator_sum{};
    const std::vector<char>    generic_code{'g'};
    const std::vector<char>    exact_code{'e'};

    RTCCache cache;
    cache.store_code_object("generic-fallback", "gfx12-generic", generator_sum, generic_code);
    EXPECT_EQ(cache.get_code_object("generic-fallback", "gfx1200", generator_sum),
              generic_code);

    cache.store_code_object("generic-fallback", "gfx1200", generator_sum, exact_code);
    EXPECT_EQ(cache.get_code_object("generic-fallback", "gfx1200", generator_sum), exact_code);
}

// make sure RTC gracefully handles a helper process that crashes
TEST(rocfft_internal, rtc_helper_crash)
{
#ifdef _WIN32
    char filename[MAX_PATH];
    GetModuleFileNameA(NULL, filename, MAX_PATH);
    fs::path test_exe    = filename;
    fs::path crasher_exe = test_exe.replace_filename("rtc_helper_crash.exe");
#else
    fs::path test_exe    = program_invocation_name;
    fs::path crasher_exe = test_exe.replace_filename("rtc_helper_crash");
#endif

    // don't touch the cache, to force compilation
    EnvironmentSetTemp env_read("ROCFFT_RTC_CACHE_READ_DISABLE", "1");
    EnvironmentSetTemp env_write("ROCFFT_RTC_CACHE_WRITE_DISABLE", "1");
    // force out-of-process compile
    EnvironmentSetTemp env_process("ROCFFT_RTC_PROCESS", "1");

    // build a trivial kernel for some specific arch
    auto generator_func = [](const std::string&) { return std::string(simple_kernel_jit); };
    kernel_src_gen_t generator(generator_func);
    const auto       code = RTCCache::cached_compile(
        "simple_kernel", "gfx1201", generator, std::array<char, 32>{}, crasher_exe.string());

    // we should get compiled code back
    ASSERT_FALSE(code.empty());
}
