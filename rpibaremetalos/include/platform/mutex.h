// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

class Mutex
{
public:
    Mutex() = default;

    void lock()
    {
        locked_ = true;
    }

    bool try_lock()
    {
        if (!locked_)
        {
            locked_ = true;
            return true;
        }

        return false;
    }

    void unlock()
    {
        locked_ = false;
    }

private:
    bool locked_ = false;
};

class LockGuard
{
public:
    LockGuard(Mutex &mutex)
        : mutex_(mutex)
    {
        mutex_.lock();
    }

    ~LockGuard()
    {
        mutex_.unlock();
    }

    Mutex & operator=(Mutex &) = delete;

private:
    Mutex &mutex_;
};
