#pragma once
#include "commonObjects.h"

namespace fb2k {
	//! \since 2.0
	class NOVTABLE console_notify {
	public:
		virtual void onConsoleRefresh() = 0;
		virtual void onConsoleLines(size_t oldLinesGone, arrayRef newLines, arrayRef newLinesTS) { (void)oldLinesGone; (void)newLines; (void)newLinesTS; onConsoleRefresh(); }
	};
	//! \since 2.0
	class NOVTABLE console_manager : public service_base {
		FB2K_MAKE_SERVICE_COREAPI(console_manager);
	public:
		virtual void clearBacklog() = 0;
		virtual fb2k::arrayRef getLines() = 0;
		virtual fb2k::arrayRef getLinesTimestamped() = 0;
		virtual void addNotify(console_notify* notify) = 0;
		virtual void removeNotify(console_notify* notify) = 0;
		//! Obsolete, done implicitly by toggling logging, do not use.
		virtual void saveBacklog() = 0;
		//! Always true, reserved for future use.
		virtual bool isVerbose() = 0;
	};
} // namespace fb2k

namespace console {
	void addNotify(fb2k::console_notify*);
	void removeNotify(fb2k::console_notify*);
	fb2k::arrayRef getLines();
	void clearBacklog();
}
