#pragma once

#include <type_traits> // std::remove_reference

#ifdef _WIN32
#include "synchro_win.h"
#else
#include "synchro_nix.h"
#endif

namespace pfc {
    class dummyLock {
    public:
        void enterRead() noexcept {}
        void enterWrite() noexcept {}
        void leaveRead() noexcept {}
        void leaveWrite() noexcept {}
        void enter() noexcept {}
        void leave() noexcept {}
        void lock() noexcept {}
        void unlock() noexcept {}
    };
    
    template<typename mutex_t>
    class mutexScope_ {
    private:
        typedef mutexScope_<mutex_t> self_t;
    public:
        mutexScope_( mutex_t * m ) noexcept : m_mutex(m) { m_mutex->enter(); }
        mutexScope_( mutex_t & m ) noexcept : m_mutex(&m) { m_mutex->enter(); }
        ~mutexScope_( ) noexcept {m_mutex->leave();}
    private:
        void operator=( const self_t & ) = delete;
        mutexScope_(const self_t & ) = delete;
        
        mutex_t * m_mutex;
    };
    typedef mutexScope_<mutexBase_t> mutexScope;

    template<typename lock_t>
    class _readWriteLock_scope_read {
    public:
        _readWriteLock_scope_read( lock_t & lock ) noexcept : m_lock( lock ) { m_lock.enterRead(); }
        ~_readWriteLock_scope_read() noexcept {m_lock.leaveRead();}
    private:
        _readWriteLock_scope_read( const _readWriteLock_scope_read &) = delete;
        void operator=( const _readWriteLock_scope_read &) = delete;
        lock_t & m_lock;
    };
    template<typename lock_t>
    class _readWriteLock_scope_write {
    public:
        _readWriteLock_scope_write( lock_t & lock ) noexcept : m_lock( lock ) { m_lock.enterWrite(); }
        ~_readWriteLock_scope_write() noexcept {m_lock.leaveWrite();}
    private:
        _readWriteLock_scope_write( const _readWriteLock_scope_write &) = delete;
        void operator=( const _readWriteLock_scope_write &) = delete;
        lock_t & m_lock;
    };
}


#define PFC_INSYNC_READ( X ) ::pfc::_readWriteLock_scope_read<typename std::remove_reference<decltype(X)>::type > _asdf_l_readWriteLock_scope_read( X )
#define PFC_INSYNC_WRITE( X ) ::pfc::_readWriteLock_scope_write<typename std::remove_reference<decltype(X)>::type > _asdf_l_readWriteLock_scope_write( X )

typedef pfc::mutexScope c_insync;

#define PFC_INSYNC(X) pfc::mutexScope_< typename std::remove_reference<decltype(X)>::type > blah____sync(X)



// Legacy macros
#define inReadSync( X ) PFC_INSYNC_READ(X)
#define inWriteSync( X ) PFC_INSYNC_WRITE(X)
#define insync( X ) PFC_INSYNC(X)

