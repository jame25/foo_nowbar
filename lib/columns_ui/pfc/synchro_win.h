#pragma once

class _critical_section_base {
protected:
	CRITICAL_SECTION sec;
public:
	_critical_section_base() = default;
	inline void enter() noexcept {EnterCriticalSection(&sec);}
	inline void leave() noexcept {LeaveCriticalSection(&sec);}
	inline void create() noexcept {
#ifdef PFC_WINDOWS_DESKTOP_APP
		InitializeCriticalSection(&sec);
#else
		InitializeCriticalSectionEx(&sec,0,0);
#endif
	}
	inline void destroy() noexcept {DeleteCriticalSection(&sec);}
	inline bool tryEnter() noexcept { return !!TryEnterCriticalSection(&sec); }
private:
	_critical_section_base(const _critical_section_base&) = delete;
	void operator=(const _critical_section_base&) = delete;
};

// Static-lifetime critical section, no cleanup - valid until process termination
class critical_section_static : public _critical_section_base {
public:
	critical_section_static() noexcept {create();}
#if !PFC_LEAK_STATIC_OBJECTS
	~critical_section_static() noexcept {destroy();}
#endif
};

// Regular critical section, intended for any lifetime scopes
class critical_section : public _critical_section_base {
public:
	critical_section() noexcept {create();}
	~critical_section() noexcept {destroy();}
};

namespace pfc {

// Read write lock - Vista-and-newer friendly lock that allows concurrent reads from a resource that permits such
// Warning, non-recursion proof
class readWriteLock {
public:
	readWriteLock() = default;
	
	void enterRead() noexcept {
		AcquireSRWLockShared( & theLock );
	}
	void enterWrite() noexcept {
		AcquireSRWLockExclusive( & theLock );
	}
	void leaveRead() noexcept {
		ReleaseSRWLockShared( & theLock );
	}
	void leaveWrite() noexcept {
		ReleaseSRWLockExclusive( &theLock );
	}

private:
	readWriteLock(const readWriteLock&) = delete;
	void operator=(const readWriteLock&) = delete;

	SRWLOCK theLock = SRWLOCK_INIT;
};

typedef ::_critical_section_base mutexBase_t;
typedef ::critical_section mutex;

}
