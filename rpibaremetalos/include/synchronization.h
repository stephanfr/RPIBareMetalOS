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

    void LockSpinLock(void *spinlock);
    void UnlockSpinLock(void *spinlock);
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
 * @class SpinLock
 * @brief A simple Spin Lock class for synchronization.
 *
 * The SpinLock class provides a mechanism for mutual exclusion, allowing only one task to own the Spin Lock at a time.
 * It supports basic lock and unlock operations and optionall allows the lock to track the owning task
 *
 * The Spinlock is lighter weight than the mutex.
 * 
 * @note This class is non-copyable and non-movable.
 */
class SpinLock : public LockableObject
{
    public :

    SpinLock( bool track_owning_task = false )
        : track_owning_task_(track_owning_task)
    {
    }

    ~SpinLock() = default;

    SpinLock(const SpinLock &) = delete;
    SpinLock(SpinLock &&) = delete;
    SpinLock &operator=(const SpinLock &) = delete;
    SpinLock &operator=(SpinLock &&) = delete;

    void Lock()
    {
        if( track_owning_task_ && ( owner_ == GetCurrentTaskId()))
        {
            char buffer[128];
            LogError("SpinLock already owned by current task: %s\n", owner_.ToString(buffer));
            return;
        }

        LockSpinLock(&lock_);

        if( track_owning_task_ )
        {
            owner_ = GetCurrentTaskId();
        }
    }

    void Unlock()
    {
        if(( lock_ != UNLOCKED ) && ( track_owning_task_ && ( owner_ != GetCurrentTaskId())))
        {
            char buffer[128];
            LogError("SpinLock not owned by current task: %s\n", owner_.ToString(buffer));
            return;
        }

        owner_ = UUID::NIL;
        UnlockSpinLock(&lock_);
    }

private:

    bool track_owning_task_;

    UUID owner_ = UUID::NIL;
    ALIGN uint32_t lock_ = State::UNLOCKED;
};

/**
 * @class LockGuard
 * @brief A simple RAII class for locking and unlocking a LockableObject.
 *
 * The LockGuard class provides a simple RAII mechanism for locking and unlocking a LockableObject.
 * When the LockGuard object is created, it locks the LockableObject. When the LockGuard object goes out of scope,
 * it unlocks the LockableObject.
 *
 * @note This class is non-copyable and non-movable.
 */

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

