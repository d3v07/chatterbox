#include <chatterbox/ipc/semaphore.hpp>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#include <fcntl.h>
#endif

namespace chatterbox {
namespace ipc {

// Semaphore implementation
Semaphore::Semaphore(key_t key, int num_sems, bool create, int permissions)
    : sem_id_(-1)
    , num_sems_(num_sems)
    , key_(key)
    , is_owner_(create)
{
    int flags = permissions;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }

    sem_id_ = semget(key, num_sems, flags);

    if (sem_id_ < 0 && create && errno == EEXIST) {
        // Semaphore already exists, try to remove and recreate
        int existing_id = semget(key, 0, 0);
        if (existing_id >= 0) {
            semctl(existing_id, 0, IPC_RMID);
        }
        sem_id_ = semget(key, num_sems, permissions | IPC_CREAT);
    }

    if (sem_id_ < 0 && !create) {
        // Try to open existing semaphore
        sem_id_ = semget(key, 0, 0);
    }

    if (sem_id_ < 0) {
        throw std::runtime_error("Failed to create/open semaphore: " +
                                std::string(strerror(errno)));
    }

    // Initialize all semaphores to 1 if we created them
    if (create) {
        for (int i = 0; i < num_sems; ++i) {
            set_value(i, 1);
        }
    }
}

Semaphore::Semaphore(Semaphore&& other) noexcept
    : sem_id_(other.sem_id_)
    , num_sems_(other.num_sems_)
    , key_(other.key_)
    , is_owner_(other.is_owner_)
{
    other.sem_id_ = -1;
    other.is_owner_ = false;
}

Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
    if (this != &other) {
        cleanup();
        sem_id_ = other.sem_id_;
        num_sems_ = other.num_sems_;
        key_ = other.key_;
        is_owner_ = other.is_owner_;
        other.sem_id_ = -1;
        other.is_owner_ = false;
    }
    return *this;
}

Semaphore::~Semaphore() {
    cleanup();
}

void Semaphore::cleanup() {
    if (is_owner_ && sem_id_ >= 0) {
        semctl(sem_id_, 0, IPC_RMID);
    }
    sem_id_ = -1;
}

ErrorCode Semaphore::wait(int sem_num, bool blocking) {
    if (sem_id_ < 0 || sem_num >= num_sems_) {
        return ErrorCode::ERR_IPC_ATTACH;
    }

    struct sembuf op;
    op.sem_num = static_cast<unsigned short>(sem_num);
    op.sem_op = -1;
    op.sem_flg = blocking ? 0 : IPC_NOWAIT;

    if (semop(sem_id_, &op, 1) < 0) {
        if (errno == EAGAIN) {
            return ErrorCode::ERR_TIMEOUT;
        }
        return ErrorCode::ERR_IPC_ATTACH;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode Semaphore::signal(int sem_num) {
    if (sem_id_ < 0 || sem_num >= num_sems_) {
        return ErrorCode::ERR_IPC_ATTACH;
    }

    struct sembuf op;
    op.sem_num = static_cast<unsigned short>(sem_num);
    op.sem_op = 1;
    op.sem_flg = 0;

    if (semop(sem_id_, &op, 1) < 0) {
        return ErrorCode::ERR_IPC_ATTACH;
    }

    return ErrorCode::SUCCESS;
}

bool Semaphore::try_wait(int sem_num) {
    return wait(sem_num, false) == ErrorCode::SUCCESS;
}

int Semaphore::get_value(int sem_num) const {
    if (sem_id_ < 0 || sem_num >= num_sems_) {
        return -1;
    }
    return semctl(sem_id_, sem_num, GETVAL);
}

void Semaphore::set_value(int sem_num, int value) {
    if (sem_id_ < 0 || sem_num >= num_sems_) {
        return;
    }
    semun arg;
    arg.val = value;
    semctl(sem_id_, sem_num, SETVAL, arg);
}

void Semaphore::remove(key_t key) {
    int sem_id = semget(key, 0, 0);
    if (sem_id >= 0) {
        semctl(sem_id, 0, IPC_RMID);
    }
}

// SemaphoreGuard implementation
SemaphoreGuard::SemaphoreGuard(Semaphore& sem, int sem_num)
    : sem_(sem)
    , sem_num_(sem_num)
    , locked_(false)
{
    if (sem_.wait(sem_num_) == ErrorCode::SUCCESS) {
        locked_ = true;
    }
}

SemaphoreGuard::~SemaphoreGuard() {
    release();
}

void SemaphoreGuard::release() {
    if (locked_) {
        sem_.signal(sem_num_);
        locked_ = false;
    }
}

// RWLock implementation
RWLock::RWLock(key_t key, bool create)
    : sem_(key, 3, create)
{
    if (create) {
        sem_.set_value(SEM_READERS, 0);  // Reader count starts at 0
        sem_.set_value(SEM_WRITER, 1);   // Writer lock available
        sem_.set_value(SEM_MUTEX, 1);    // Mutex available
    }
}

RWLock::~RWLock() = default;

void RWLock::read_lock() {
    sem_.wait(SEM_MUTEX);
    int readers = sem_.get_value(SEM_READERS);
    if (readers == 0) {
        sem_.wait(SEM_WRITER);  // First reader blocks writers
    }
    // Increment reader count (done by manipulating semaphore)
    semun arg;
    arg.val = readers + 1;
    semctl(sem_.get_id(), SEM_READERS, SETVAL, arg);
    sem_.signal(SEM_MUTEX);
}

void RWLock::read_unlock() {
    sem_.wait(SEM_MUTEX);
    int readers = sem_.get_value(SEM_READERS);
    semun arg;
    arg.val = readers - 1;
    semctl(sem_.get_id(), SEM_READERS, SETVAL, arg);
    if (readers - 1 == 0) {
        sem_.signal(SEM_WRITER);  // Last reader unblocks writers
    }
    sem_.signal(SEM_MUTEX);
}

bool RWLock::try_read_lock() {
    if (!sem_.try_wait(SEM_MUTEX)) {
        return false;
    }
    int readers = sem_.get_value(SEM_READERS);
    if (readers == 0) {
        if (!sem_.try_wait(SEM_WRITER)) {
            sem_.signal(SEM_MUTEX);
            return false;
        }
    }
    semun arg;
    arg.val = readers + 1;
    semctl(sem_.get_id(), SEM_READERS, SETVAL, arg);
    sem_.signal(SEM_MUTEX);
    return true;
}

void RWLock::write_lock() {
    sem_.wait(SEM_WRITER);
}

void RWLock::write_unlock() {
    sem_.signal(SEM_WRITER);
}

bool RWLock::try_write_lock() {
    return sem_.try_wait(SEM_WRITER);
}

// ReadLockGuard implementation
ReadLockGuard::ReadLockGuard(RWLock& lock) : lock_(lock) {
    lock_.read_lock();
}

ReadLockGuard::~ReadLockGuard() {
    lock_.read_unlock();
}

// WriteLockGuard implementation
WriteLockGuard::WriteLockGuard(RWLock& lock) : lock_(lock) {
    lock_.write_lock();
}

WriteLockGuard::~WriteLockGuard() {
    lock_.write_unlock();
}

// NamedSemaphore implementation
NamedSemaphore::NamedSemaphore(const std::string& name, bool create, unsigned int initial_value)
    : name_(name)
    , sem_(nullptr)
    , is_owner_(create)
    , valid_(false)
{
#ifdef __APPLE__
    // Use dispatch semaphore on macOS
    auto* dsem = dispatch_semaphore_create(static_cast<long>(initial_value));
    if (dsem) {
        sem_ = dsem;
        valid_ = true;
    }
#else
    // Use POSIX named semaphore on Linux
    int flags = create ? (O_CREAT | O_EXCL) : 0;
    sem_t* s = sem_open(name.c_str(), flags, 0644, initial_value);
    if (s == SEM_FAILED && create) {
        // Try to unlink and recreate
        sem_unlink(name.c_str());
        s = sem_open(name.c_str(), O_CREAT, 0644, initial_value);
    }
    if (s != SEM_FAILED) {
        sem_ = s;
        valid_ = true;
    }
#endif
}

NamedSemaphore::~NamedSemaphore() {
#ifdef __APPLE__
    // Dispatch semaphores are reference counted
    if (sem_) {
        dispatch_release(static_cast<dispatch_semaphore_t>(sem_));
    }
#else
    if (sem_) {
        sem_close(static_cast<sem_t*>(sem_));
        if (is_owner_) {
            sem_unlink(name_.c_str());
        }
    }
#endif
}

void NamedSemaphore::wait() {
    if (!valid_) return;
#ifdef __APPLE__
    dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(sem_), DISPATCH_TIME_FOREVER);
#else
    sem_wait(static_cast<sem_t*>(sem_));
#endif
}

void NamedSemaphore::signal() {
    if (!valid_) return;
#ifdef __APPLE__
    dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(sem_));
#else
    sem_post(static_cast<sem_t*>(sem_));
#endif
}

bool NamedSemaphore::try_wait() {
    if (!valid_) return false;
#ifdef __APPLE__
    return dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(sem_),
                                   DISPATCH_TIME_NOW) == 0;
#else
    return sem_trywait(static_cast<sem_t*>(sem_)) == 0;
#endif
}

void NamedSemaphore::unlink(const std::string& name) {
#ifndef __APPLE__
    sem_unlink(name.c_str());
#else
    (void)name; // Dispatch semaphores don't use names
#endif
}

} // namespace ipc
} // namespace chatterbox
