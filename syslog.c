#include "syslog.h"

// these do nothing

void closelog (void) {}
void openlog (const char *__ident, int __option, int __facility) {}
int setlogmask (int __mask) {
	return 0;
}
void syslog (int __pri, const char *__fmt, ...) {}
