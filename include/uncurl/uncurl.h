#ifndef __UNCURL_H
#define __UNCURL_H

#if defined(UNCURL_MAKE_SHARED)
	#define UNCURL_EXPORT __declspec(dllexport)
#else
	#define UNCURL_EXPORT
#endif

#include <stdint.h>

#include "status.h"
#include "const.h"

struct uncurl;
struct uncurl_conn;

struct uncurl_info {
	int32_t scheme;
	char *host;
	uint16_t port;
	char *path;
};

#ifdef __cplusplus
extern "C" {
#endif

UNCURL_EXPORT int32_t uncurl_init(struct uncurl **uc_in);
UNCURL_EXPORT int32_t uncurl_connect(struct uncurl *uc, struct uncurl_conn **ucc_in,
	int32_t scheme, char *host, uint16_t port);
UNCURL_EXPORT int32_t uncurl_write_header(struct uncurl_conn *ucc, char *method, char *path);
UNCURL_EXPORT int32_t uncurl_write_body(struct uncurl_conn *ucc, char *body, uint32_t body_len);
UNCURL_EXPORT int32_t uncurl_read_header(struct uncurl_conn *ucc);
UNCURL_EXPORT int32_t uncurl_get_status_code(struct uncurl_conn *ucc, int32_t *status_code);
UNCURL_EXPORT int32_t uncurl_read_body_all(struct uncurl_conn *ucc, char **body, uint32_t *body_len);
UNCURL_EXPORT void uncurl_close(struct uncurl_conn *ucc);
UNCURL_EXPORT void uncurl_destroy(struct uncurl *uc);

UNCURL_EXPORT void uncurl_set_header_str(struct uncurl_conn *ucc, char *name, char *value);
UNCURL_EXPORT void uncurl_set_header_int(struct uncurl_conn *ucc, char *name, int32_t value);

UNCURL_EXPORT int32_t uncurl_parse_url(char *url, struct uncurl_info *uci);
UNCURL_EXPORT void uncurl_free_info(struct uncurl_info *uci);
UNCURL_EXPORT void uncurl_clear_header(struct uncurl_conn *ucc);
UNCURL_EXPORT int8_t uncurl_check_header(struct uncurl_conn *ucc, char *name, char *subval);
UNCURL_EXPORT int32_t uncurl_set_cacert(struct uncurl *uc, char **cacert, uint32_t num_certs);
UNCURL_EXPORT int32_t uncurl_set_cacert_file(struct uncurl *uc, char *cacert_file);
UNCURL_EXPORT void uncurl_set_option(struct uncurl *uc, int32_t opt, int32_t val);

UNCURL_EXPORT int32_t uncurl_get_header(struct uncurl_conn *ucc, char *key, int32_t *val_int, char **val_str);

#define uncurl_get_header_int(ucc, key, val_int) uncurl_get_header(ucc, key, val_int, NULL)
#define uncurl_get_header_str(ucc, key, val_str) uncurl_get_header(ucc, key, NULL, val_str)

//XXX uncurl_read_body(struct uncurl_conn *ucc, char *buf, uint32_t buf_len, uint32_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif
