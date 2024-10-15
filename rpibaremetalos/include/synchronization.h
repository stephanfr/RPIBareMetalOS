// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "services/uuid.h"

#include "devices/log.h"

UUID GetCurrentTaskId(void);

extern "C"
{
    void LockMutex(void *mutex);
    void UnlockMutex(void *mutex);
}

class Mutex
{
    public :

    typedef enum State
    {
        UNLOCKED = 0,
        LOCKED = 1
    } State;

    Mutex() = default;
    ~Mutex() = default;

    Mutex(const Mutex &) = delete;
    Mutex(Mutex &&) = delete;
    Mutex &operator=(const Mutex &) = delete;
    Mutex &operator=(Mutex &&) = delete;

    void Lock()
    {
        if(owner_ == GetCurrentTaskId())
        {
            LogError("Mutex already owned by current task\n");
            return;
        }

        LockMutex(&lock_);
        owner_ = GetCurrentTaskId();
    }

    void Unlock()
    {
        if(( lock_ != 0 ) && ( owner_ != GetCurrentTaskId()))
        {
            LogError("Mutex not owned by current task\n");
            return;
        }

        owner_ = UUID::NIL;
        UnlockMutex(&lock_);
    }

private:

    uint32_t lock_ = State::UNLOCKED;
    UUID owner_ = UUID::NIL;
};

template <typename T>
class LockGuard
{
    public:
    LockGuard(T &synch_object)
        : synch_object_(synch_object)
    {
        synch_object_.Lock();
    }

    ~LockGuard()
    {
        synch_object_.Unlock();
    }

    void Unlock()
    {
        synch_object_.Unlock();
    }

private:
    T &synch_object_;
};

