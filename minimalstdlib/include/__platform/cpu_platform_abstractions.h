// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  This file contains conditional compilation for different CPU/OS combinations
//      primarily either X64 or ARM64 on linux to permit unit and performance
//      testing of lockfree classes, like the lockfree memory allocator.
//
//  This should be the only location in the project with this type of platform
//      specific code, so if porting to a new CPU or OS this ought to be the
//      only place to add new code.

#pragma once

#include "minstdconfig.h"
#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace platform
        {
#if defined(__MINIMAL_STD_TEST__)
            using test_cpu_id_provider_type = uint32_t (*)();
            inline test_cpu_id_provider_type test_cpu_id_provider_ = nullptr;

            inline void set_test_cpu_id_provider(test_cpu_id_provider_type provider)
            {
                test_cpu_id_provider_ = provider;
            }

            inline void clear_test_cpu_id_provider()
            {
                test_cpu_id_provider_ = nullptr;
            }
#endif

#if defined(__x86_64__) || defined(_M_X64)
            inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t &eax, uint32_t &ebx, uint32_t &ecx, uint32_t &edx)
            {
                __asm__ volatile(
                    "cpuid"
                    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                    : "a"(leaf), "c"(subleaf));
            }
#endif


            /**
             * @brief Returns a monotonically increasing counter value.
             *
             * This function provides a high-resolution, monotonically increasing counter
             * suitable for ordering events in lock-free algorithms. The counter is per-CPU
             * and may not be synchronized across CPUs, but is guaranteed to be monotonic
             * on any single CPU.
             *
             * On x64: Uses RDTSC (Time Stamp Counter)
             * On ARM64: Uses CNTVCT_EL0 (Counter-timer Virtual Count)
             *
             * @return A 64-bit monotonically increasing counter value.
             */
            inline uint64_t get_monotonic_counter()
            {
#if defined(__x86_64__) || defined(_M_X64)
                uint32_t lo, hi;
                __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
                return (static_cast<uint64_t>(hi) << 32) | lo;

#elif defined(__aarch64__)
                uint64_t counter;
                __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
                return counter;

#else
#error "Unsupported architecture for get_monotonic_counter()"
#endif
            }

            /**
             * @brief Returns the current CPU/core ID.
             *
             * This function returns an identifier for the current CPU core. This can be
             * used for per-CPU sharding to reduce contention in multi-core systems.
             *
             * On x64: Uses CPUID instruction with leaf 0x1 (returns initial APIC ID)
             * On ARM64: Uses MPIDR_EL1 (Multiprocessor Affinity Register)
             *
             * Note: The returned value may not be contiguous (0, 1, 2, ...) but is
             * guaranteed to be unique per CPU core.
             *
             * @return A CPU identifier value.
             */
            inline uint32_t get_cpu_id()
            {
#if defined(__MINIMAL_STD_TEST__)
                if (test_cpu_id_provider_ == nullptr)
                {
                    MINIMAL_STD_FAIL(test_cpu_id_provider_not_set_for_test_mode);
                    return 0u;
                }

                return test_cpu_id_provider_();

#elif defined(__x86_64__) || defined(_M_X64)
                uint32_t ebx;
                __asm__ volatile(
                    "movl $1, %%eax\n\t"
                    "cpuid"
                    : "=b"(ebx)
                    :
                    : "eax", "ecx", "edx");
                // Initial APIC ID is in bits 31:24 of EBX
                return (ebx >> 24) & 0xFF;

#elif defined(__aarch64__)
                uint64_t mpidr;
                __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
                // Extract Aff0 (bits 7:0) which typically represents the core ID within a cluster
                // For more complex topologies, you may need Aff1, Aff2, Aff3 as well
                return static_cast<uint32_t>(mpidr & 0xFF);

#else
#error "Unsupported architecture for get_cpu_id()"
#endif
            }

            /**
             * @brief Returns the number of available CPU cores.
             *
             * On Linux: Uses sysconf(_SC_NPROCESSORS_ONLN).
             * On x64 bare-metal: Uses CPUID topology leaves when available.
             * On ARM64 bare-metal: Returns 1.
             * On other platforms: Returns 1.
             *
             * @return Number of online CPU cores (>= 1).
             */
            inline uint32_t get_cpu_count()
            {
#if defined(__MINIMAL_STD_TEST__)
                return 1u;

#elif defined(__x86_64__) || defined(_M_X64)
                uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
                cpuid(0, 0, eax, ebx, ecx, edx);
                uint32_t max_leaf = eax;

                if (max_leaf >= 0x0B)
                {
                    uint32_t logical_count = 0;

                    for (uint32_t level = 0; level < 8; ++level)
                    {
                        cpuid(0x0B, level, eax, ebx, ecx, edx);
                        uint32_t level_type = (ecx >> 8) & 0xFF;
                        if (level_type == 0)
                        {
                            break;
                        }

                        if (level_type == 2)
                        {
                            logical_count = ebx & 0xFFFF;
                            break;
                        }
                    }

                    if (logical_count != 0)
                    {
                        return logical_count;
                    }
                }

                if (max_leaf >= 0x01)
                {
                    cpuid(0x01, 0, eax, ebx, ecx, edx);
                    uint32_t logical_count = (ebx >> 16) & 0xFF;
                    return (logical_count != 0) ? logical_count : 1u;
                }

                return 1u;
#else
                return 1u;
#endif
            }

            /**
             * @brief Hint to the CPU that we are in a spin-wait loop.
             *
             * This can reduce power or contention while spinning. Note that on modern
             * x86_64 architectures (Skylake and newer), PAUSE takes ~140 cycles, whereas
             * on AArch64, YIELD is typically much faster. Use the back_off() function
             * which normalizes these latency differences.
             */
            inline void cpu_relax()
            {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ __volatile__("yield" ::: "memory");
#else
                __asm__ __volatile__("" ::: "memory");
#endif
            }

            /**
             * @brief Standardized exponential backoff for lock-free contention.
             *
             * On x64, PAUSE is ~140 cycles on modern architectures, so we use a small multiplier.
             * On AArch64, YIELD is much faster, so we use a larger multiplier.
             */
            inline void back_off(size_t &retries)
            {
                const size_t bounded_retries = (retries > 256) ? 256 : retries;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
                const size_t spin_count = 2 + (bounded_retries * 2);
#else
                const size_t spin_count = 32 + (bounded_retries * 32);
#endif
                for (size_t i = 0; i < spin_count; ++i)
                {
                    cpu_relax();
                }

                retries++;
            }

            /**
             * @brief Check if a 128-bit aligned chunk contains all ones.
             *
             * This function uses SIMD instructions when available to efficiently
             * check if two consecutive 64-bit words are both ~0ULL. Used for
             * optimistic scanning in lock-free bitset structures.
             *
             * On x64: Uses SSE2 instructions (_mm_load_si128, _mm_cmpeq_epi8)
             * On ARM64: Uses NEON instructions (vld1q_u64, vceqq_u64)
             * On other platforms: Falls back to scalar loads
             *
             * @param chunk_ptr Pointer to two consecutive uint64_t values (must be 16-byte aligned on x64)
             * @return true if both words are all ones (~0ULL), false otherwise
             */
            inline bool simd_scan_128bit_is_all_ones(const uint64_t *chunk_ptr)
            {
#if defined(__x86_64__) || defined(_M_X64)
                __m128i data = _mm_load_si128(reinterpret_cast<const __m128i *>(chunk_ptr));
                __m128i all_ff = _mm_set1_epi8(static_cast<char>(0xFF));
                __m128i cmp = _mm_cmpeq_epi8(data, all_ff);
                return _mm_movemask_epi8(cmp) == 0xFFFF;
#elif defined(__aarch64__)
                uint64x2_t data = vld1q_u64(chunk_ptr);
                uint64x2_t all_ff = vdupq_n_u64(~0ULL);
                uint64x2_t cmp = vceqq_u64(data, all_ff);
                return (vgetq_lane_u64(cmp, 0) & vgetq_lane_u64(cmp, 1)) == ~0ULL;
#else
                return (chunk_ptr[0] == ~0ULL) && (chunk_ptr[1] == ~0ULL);
#endif
            }

            struct default_platform_provider
            {
                static inline uint32_t get_cpu_id()
                {
                    return platform::get_cpu_id();
                }

                static inline uint64_t get_monotonic_counter()
                {
                    return platform::get_monotonic_counter();
                }

                static inline void cpu_relax()
                {
                    platform::cpu_relax();
                }

                static inline void back_off(size_t &retries)
                {
                    platform::back_off(retries);
                }
            };

        } // namespace platform
    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
