#ifndef HTTP_SERVER_CORE_CONFIG_HPP_INCLUDED
#define HTTP_SERVER_CORE_CONFIG_HPP_INCLUDED

#include "syslogger.hpp"
#include "core_version.hpp"

#if defined(__GNUG__)
#	pragma GCC system_header
#endif

#define HTTP_SERVER_LOG_PREFIX "HTTP SERVER CORE"
#define HFUNCTION_PREFIX __FUNCTION__

#define HCORE_LOG_MAX_MESSAGE_SIZE 4096*3

#define HCORE_LOG_GENERIC(log_type, ...)																		\
do {																											\
	char vat_[HCORE_LOG_MAX_MESSAGE_SIZE];																		\
	std::memset(vat_, '\0', HCORE_LOG_MAX_MESSAGE_SIZE);														\
	std::sprintf(vat_, __VA_ARGS__);																			\
	log_type("%s [%s] %s %s", HTTP_SERVER_LOG_PREFIX, CORE_VERSION_STRING, HFUNCTION_PREFIX, vat_)				\
} while(0); 

#if defined (T2H_DEBUG)
#	define HCORE_TRACE(...) \
		HCORE_LOG_GENERIC(LOG_TRACE, __VA_ARGS__)
#else
#	define HCORE_TRACE(...) { /* */ }
#endif // T2H_DEBUG

#define HCORE_WARNING(...) \
	HCORE_LOG_GENERIC(LOG_WARNING, __VA_ARGS__)

#define HCORE_ERROR(...) \
	HCORE_LOG_GENERIC(LOG_ERROR, __VA_ARGS__)


#endif 

