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
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

//  When running in userspace (e.g., unit tests), we cannot execute privileged
//  interrupt control instructions. Define this macro to make interrupt guards
//  no-ops for testing purposes.
#if defined(__MINIMAL_STD_TEST__) || defined(PLATFORM_USERSPACE)
#define PLATFORM_INTERRUPT_GUARD_NOOP
#endif

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace platform
        {
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
#if defined(__linux__) && (defined(__MINIMAL_STD_TEST__) || defined(PLATFORM_USERSPACE))
                int cpu = sched_getcpu();
                return (cpu < 0) ? 0u : static_cast<uint32_t>(cpu);

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
#if defined(__linux__)
                long count = sysconf(_SC_NPROCESSORS_ONLN);
                return (count > 0) ? static_cast<uint32_t>(count) : 1u;

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
             * This can reduce power or contention while spinning.
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

            /**
             * @brief Interrupt state type for save/restore operations.
             *
             * On x64: Contains the RFLAGS register value
             * On ARM64: Contains the DAIF register value
             */
            using interrupt_state_t = uint64_t;

#ifndef PLATFORM_INTERRUPT_GUARD_NOOP

            /**
             * @brief Disables interrupts and returns the previous interrupt state.
             *
             * This function disables interrupts on the current CPU and returns the
             * previous interrupt state, which should be passed to restore_interrupts()
             * to restore the original state.
             *
             * On x64: Clears the IF flag in RFLAGS using CLI
             * On ARM64: Sets the I bit in DAIF to mask IRQ interrupts
             *
             * @return The previous interrupt state for later restoration.
             */
            inline interrupt_state_t disable_interrupts()
            {
#if defined(__x86_64__) || defined(_M_X64)
                interrupt_state_t flags;
                __asm__ volatile(
                    "pushfq\n\t"
                    "pop %0\n\t"
                    "cli"
                    : "=r"(flags)
                    :
                    : "memory");
                return flags;

#elif defined(__aarch64__)
                interrupt_state_t daif;
                __asm__ volatile(
                    "mrs %0, daif\n\t"
                    "msr daifset, #2" // Set I bit to mask IRQ
                    : "=r"(daif)
                    :
                    : "memory");
                return daif;

#else
#error "Unsupported architecture for disable_interrupts()"
#endif
            }

            /**
             * @brief Restores the interrupt state to a previously saved value.
             *
             * This function restores the interrupt state to the value returned by
             * a previous call to disable_interrupts(). If interrupts were enabled
             * before disable_interrupts() was called, they will be re-enabled.
             *
             * @param state The interrupt state to restore (from disable_interrupts()).
             */
            inline void restore_interrupts(interrupt_state_t state)
            {
#if defined(__x86_64__) || defined(_M_X64)
                __asm__ volatile(
                    "push %0\n\t"
                    "popfq"
                    :
                    : "r"(state)
                    : "memory", "cc");

#elif defined(__aarch64__)
                __asm__ volatile(
                    "msr daif, %0"
                    :
                    : "r"(state)
                    : "memory");

#else
#error "Unsupported architecture for restore_interrupts()"
#endif
            }

#else // PLATFORM_INTERRUPT_GUARD_NOOP

            /**
             * @brief No-op version of disable_interrupts for userspace testing.
             *
             * When PLATFORM_INTERRUPT_GUARD_NOOP is defined (e.g., during unit tests),
             * interrupt control is disabled since privileged instructions cannot be
             * executed in userspace.
             *
             * @return Always returns 0.
             */
            inline interrupt_state_t disable_interrupts()
            {
                return 0;
            }

            /**
             * @brief No-op version of restore_interrupts for userspace testing.
             *
             * @param state Ignored in the no-op implementation.
             */
            inline void restore_interrupts(interrupt_state_t /* state */)
            {
            }

#endif // PLATFORM_INTERRUPT_GUARD_NOOP

            /**
             * @brief RAII guard for interrupt-protected critical sections.
             *
             * This class provides exception-safe interrupt protection. Interrupts are
             * disabled when the guard is constructed and restored to their previous
             * state when the guard is destroyed.
             *
             * When PLATFORM_INTERRUPT_GUARD_NOOP is defined, this becomes a no-op
             * for userspace testing.
             *
             * Usage:
             *     {
             *         interrupt_guard guard;
             *         // Critical section - interrupts are disabled
             *     } // Interrupts restored here
             */
            class interrupt_guard
            {
            public:
                interrupt_guard() : saved_state_(disable_interrupts())
                {
                }

                ~interrupt_guard()
                {
                    restore_interrupts(saved_state_);
                }

                // Non-copyable and non-movable
                interrupt_guard(const interrupt_guard &) = delete;
                interrupt_guard(interrupt_guard &&) = delete;
                interrupt_guard &operator=(const interrupt_guard &) = delete;
                interrupt_guard &operator=(interrupt_guard &&) = delete;

            private:
                interrupt_state_t saved_state_;
            };

        } // namespace platform
    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
