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
		constexpr char BUILD_TYPE[] = "release";
#else
		setlogmask(LOG_UPTO(LOG_DEBUG));
		constexpr char BUILD_TYPE[] = "debug";
#endif
		openlog("sykerolabs", LOG_CONS | LOG_PID | LOG_NDELAY, facility);

		syslog(LOG_INFO, "build date: %s", __DATE__);
		syslog(LOG_INFO, "build time: %s", __TIME__);
		syslog(LOG_INFO, "build type: %s", BUILD_TYPE);

		syslog(LOG_INFO, "started from %s", path);
	}

	facility::~facility()
	{
		syslog(LOG_INFO, "stopped!");

		closelog();
	}
}