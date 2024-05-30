#include "mega.pch"
#include "sykero_log.hpp"

#include <syslog.h>

namespace sl::log
{
	facility::facility(int facility, const char* path)
	{
		assert(path);

#ifdef NDEBUG
		setlogmask(LOG_UPTO(LOG_INFO));
#else
		setlogmask(LOG_UPTO(LOG_DEBUG));
#endif
		openlog("sykerolabs", LOG_CONS | LOG_PID | LOG_NDELAY, facility);

		syslog(LOG_INFO, "started from %s", path);
		syslog(LOG_INFO, "build date: %s time: %s", __DATE__, __TIME__);
	}

	facility::~facility()
	{
		syslog(LOG_INFO, "stopped!");

		closelog();
	}
}