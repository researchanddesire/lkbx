#ifndef SOFTWARE_DASHBOARD_H
#define SOFTWARE_DASHBOARD_H

#ifndef RAD_SERVER
#error "RAD_SERVER must be set by an explicit firmware profile"
#endif

#define RAD_AUTH RAD_SERVER "/api/lockbox/auth"

#endif  // SOFTWARE_DASHBOARD_H
