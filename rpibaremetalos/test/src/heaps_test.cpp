// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "heaps.h"

namespace
{
    uint32_t constructions = 0;

    //  Larger than any heap, so every allocation of it fails.

    struct Unallocatable
    {
        Unallocatable() { constructions++; }

        uint8_t bytes_[1024ULL * 1024ULL * 1024ULL];
    };

    struct Base
    {
        virtual ~Base() = default;

        uint64_t base_value_ = 1;
    };

    struct Derived : public Base
    {
        explicit Derived(uint64_t value)
            : derived_value_(value)
        {
        }

        uint64_t derived_value_;
        uint8_t padding_[200];
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HeapAllocationFailureTests)
    {
        void setup() override
        {
            constructions = 0;
        }
    };
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HeapDerivedAllocationTests)
    {};
#pragma GCC diagnostic pop

    TEST(HeapAllocationFailureTests, ExhaustedHeapReturnsEmptyWithoutConstructing)
    {
        CHECK_FALSE(static_cast<bool>(dynamic_new<Unallocatable>()));
        CHECK_FALSE(static_cast<bool>(make_dynamic_unique<Unallocatable>()));
        CHECK_FALSE(static_cast<bool>(make_static_unique<Unallocatable>()));
        POINTERS_EQUAL(nullptr, static_new<Unallocatable>());

        CHECK_EQUAL(0, constructions);
    }

    TEST(HeapDerivedAllocationTests, BaseHandleHoldsTheDerivedObject)
    {
        minstd::unique_ptr<Base> from_dynamic_new = dynamic_new<Base, Derived>(7ULL);
        minstd::unique_ptr<Base> from_make_unique = make_dynamic_unique<Derived, Base>(9ULL);

        CHECK_EQUAL(7, static_cast<Derived &>(*from_dynamic_new).derived_value_);
        CHECK_EQUAL(9, static_cast<Derived &>(*from_make_unique).derived_value_);
    }
}
