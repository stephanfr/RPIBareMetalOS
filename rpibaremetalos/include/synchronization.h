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


class LockableObject
{
    public :

    typedef enum State
    {
        UNLOCKED = 0,
        LOCKED = 1
    } State;

    LockableObject() = default;
    ~LockableObject() = default;

    LockableObject(const LockableObject &) = delete;
    LockableObject(LockableObject &&) = delete;
    LockableObject &operator=(const LockableObject &) = delete;
    LockableObject &operator=(LockableObject &&) = delete;

    virtual void Lock() = 0;
    virtual void Unlock() = 0;
};  


/**
 * @class __TasklessMutex
 * @brief A mutex class for synchronization without task awareness.
 * 
 * This class provides a simple mutex mechanism for locking and unlocking
 * critical sections in a bare-metal environment. It is not aware of tasks
 * or threads, making it suitable for low-level synchronization.
 * 
 * Do not use this class except in cases where a critical section exists
 * outside of a task or thread context.
 * 
 * @note This class is non-copyable and non-movable.
 */
class __TasklessMutex : public LockableObject
{
    public :

    __TasklessMutex() = default;
    ~__TasklessMutex() = default;

    __TasklessMutex(const __TasklessMutex &) = delete;
    __TasklessMutex(__TasklessMutex &&) = delete;
    __TasklessMutex &operator=(const __TasklessMutex &) = delete;
    __TasklessMutex &operator=(__TasklessMutex &&) = delete;

    void Lock()
    {
        LockMutex(&lock_);
    }

    void Unlock()
    {
        UnlockMutex(&lock_);
    }

private:

    ALIGN uint32_t lock_ = State::UNLOCKED;
};

/**
 * @class Mutex
 * @brief A simple mutex class for synchronization.
 *
 * The Mutex class provides a mechanism for mutual exclusion, allowing only one task to own the mutex at a time.
 * It supports basic lock and unlock operations and ensures that the same task cannot lock the mutex multiple times.
 *
 * @note This class is non-copyable and non-movable.
 */
class Mutex : public LockableObject
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
            char buffer[128];
            LogError("Mutex already owned by current task: %s\n", owner_.ToString(buffer));
            return;
        }

        LockMutex(&lock_);
        owner_ = GetCurrentTaskId();
    }

    void Unlock()
    {
        if(( lock_ != 0 ) && ( owner_ != GetCurrentTaskId()))
        {
            char buffer[128];
            LogError("Mutex not owned by current task: %s\n", owner_.ToString(buffer));
            return;
        }

        owner_ = UUID::NIL;
        UnlockMutex(&lock_);
    }

private:

    UUID owner_ = UUID::NIL;
    ALIGN uint32_t lock_ = State::UNLOCKED;
};

class LockGuard
{
    public:
    LockGuard(LockableObject &lockable_object)
        : lockable_object_(lockable_object)
    {
        lockable_object_.Lock();
    }

    ~LockGuard()
    {
        lockable_object_.Unlock();
    }

    void Unlock()
    {
        lockable_object_.Unlock();
    }

private:
    LockableObject &lockable_object_;
};

