// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include <string.h>

template<uint32_t SIZE>
class OpaqueData 
{
    public :
        OpaqueData()
        {}

        template <typename T>
        OpaqueData( const T& data_block )
        {
            static_assert( sizeof(T) < SIZE );

            memcpy( opaque_data_block_, &data_block, sizeof(T) );
        }

        OpaqueData( const OpaqueData& data_to_copy )
        {
            memcpy( opaque_data_block_, data_to_copy.opaque_data_block_, SIZE );
        }

        template <typename T>
        T& operator=( const T   &data_to_copy )
        {
            static_assert( sizeof(T) < SIZE );

            memcpy( opaque_data_block_, &data_to_copy, sizeof(T) );

            return static_cast<T&>( *((T*)opaque_data_block_) );
        }

        template <typename T>
        operator T& ()
        {
            return static_cast<T&>( *((T*)opaque_data_block_) );
        }

        template <typename T>
        operator const T&() const
        {
            return static_cast<const T&>( *((T*)opaque_data_block_) );
        }


    private :

    ALIGN uint8_t opaque_data_block_[SIZE];
};