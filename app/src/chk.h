#ifndef _CHK_H_
#define _CHK_H_

#include <zephyr/logging/log.h>

#define CHK(X) ({ int err = X; if (err != 0) { LOG_ERR("%s returned %d (%s:%d)", #X, err, __FILE__, __LINE__); } err == 0; })

#endif
