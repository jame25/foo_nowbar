#include "foobar2000-sdk-pch.h"

#include "abort_callback.h"

void abort_callback::check() const {
    if (is_aborting()) {
        throw exception_aborted();
    }
}

void abort_callback::sleep(double p_timeout_seconds) const {
    if (!sleep_ex(p_timeout_seconds)) {
        throw exception_aborted();
    }
}

bool abort_callback::sleep_ex(double p_timeout_seconds) const {
	// return true IF NOT SET (timeout), false if set
	return !pfc::event::g_wait_for(get_abort_event(),p_timeout_seconds);
}

bool abort_callback::waitForEvent( pfc::eventHandle_t evtHandle, double timeOut ) const {
    int status = pfc::event::g_twoEventWait( this->get_abort_event(), evtHandle, timeOut );
    switch(status) {
        case 1: throw exception_aborted();
        case 2: return true;
        case 0: return false;
        default: uBugCheck();
    }
}

bool abort_callback_usehandle::is_aborting() const {
    return pfc::event::g_wait_for( get_abort_event(), 0 );
}

bool abort_callback::waitForEvent(pfc::event& evt, double timeOut) const {
	return waitForEvent(evt.get_handle(), timeOut); 
}

void abort_callback::waitForEvent(pfc::eventHandle_t evtHandle) const {
	bool status = waitForEvent(evtHandle, -1); (void)status;
	PFC_ASSERT(status); // should never return false
}

void abort_callback::waitForEvent(pfc::event& evt) const {
	bool status = waitForEvent(evt, -1); (void)status;
	PFC_ASSERT(status); // should never return false
}

bool abort_callback::waitForEventNoThrow(pfc::eventHandle_t evtHandle) const {
    int status = pfc::event::g_twoEventWait(this->get_abort_event(), evtHandle, -1);
    switch (status) {
    case 1: return false;
    case 2: return true;
    default: uBugCheck();
    }
}

bool abort_callback::waitForEventNoThrow(pfc::event& evt) const {
    return waitForEventNoThrow(evt.get_handle());
}

namespace fb2k {
	abort_callback_dummy noAbort;
}

abort_callback_event abort_callback_clone::clone(abort_callback_event arg) {
    return pfc::fileHandleDup(arg);
}

void abort_callback_clone::close(abort_callback_event arg) {
    return pfc::fileHandleClose(arg);
}

