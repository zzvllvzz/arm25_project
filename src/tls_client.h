//
// Created by HaoKun Tong on 2025/9/29.
//

#ifndef RP2040_FREERTOS_IRQ_TLS_CLIENT_H
#define RP2040_FREERTOS_IRQ_TLS_CLIENT_H

#endif //RP2040_FREERTOS_IRQ_TLS_CLIENT_H
// tls_client.h
#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif


 bool run_tls_client_test(const uint8_t *cert, size_t cert_len,
                             const char *server,
                             const char *request,
                             int timeout);
    char* run_tls_client_request(const uint8_t *cert, size_t cert_len,
                             const char *server,
                             const char *request,
                             int timeout);

#ifdef __cplusplus
}
#endif