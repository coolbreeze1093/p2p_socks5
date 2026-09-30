#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <filesystem>
#include <plog/Log.h>
#include "SimpleIni.h"

struct ClientConfig
{
    // STUN
    std::string stun_ip = "stun.miwifi.com";
    uint16_t stun_port = 3478;

    // WebSocket
    std::string ws_server_ip = "ws://127.0.0.1:80";

    uint32_t ws_max_message_size = 1024 * 1024;
    uint16_t ws_connection_timeout = 3000;
    bool ws_enable_tls = false;
    bool ws_disable_tls_verification = false;
    uint16_t ws_max_outstanding_pings = 100;
    uint16_t ws_ping_interval = 3000;

    std::string ws_ca_certificate_pem_file;
    std::string ws_key_pem_file;
    std::string ws_key_pem_pass;
    std::string ws_certificate_pem_file;
    std::string ws_proxy_server;
    std::string ws_protocols;

    // SOCKS5
    std::string socks5_bind_ip = "0.0.0.0";
    int16_t socks5_bind_port = 10801;

    // Server
    std::string server_password;

    // ASIO
    int16_t asio_thread_count = 4;

    static void readClientConfig(ClientConfig &config, std::string exec_dir)
    {
        CSimpleIniA ini;

        // 允许使用 UTF-8
        ini.SetUnicode();

        if (!std::filesystem::exists(exec_dir))
        {
            PLOG_WARNING << "config.ini not found, creating default config";

            // ============================================================
            // STUN
            // ============================================================
            ini.SetValue(
                "stun",
                "ip",
                config.stun_ip.c_str());

            ini.SetLongValue(
                "stun",
                "port",
                config.stun_port);

            // ============================================================
            // WebSocket
            // ============================================================
            ini.SetValue(
                "websocket",
                "server_ip",
                config.ws_server_ip.c_str());

            ini.SetLongValue(
                "websocket",
                "max_message_size",
                config.ws_max_message_size);

            ini.SetLongValue(
                "websocket",
                "connection_timeout",
                config.ws_connection_timeout);

            ini.SetBoolValue(
                "websocket",
                "enable_tls",
                config.ws_enable_tls);

            ini.SetBoolValue(
                "websocket",
                "disable_tls_verification",
                config.ws_disable_tls_verification);

            ini.SetLongValue(
                "websocket",
                "max_outstanding_pings",
                config.ws_max_outstanding_pings);

            ini.SetLongValue(
                "websocket",
                "ping_interval",
                config.ws_ping_interval);

            ini.SetValue(
                "websocket",
                "ca_certificate_pem_file",
                config.ws_ca_certificate_pem_file.c_str());

            ini.SetValue(
                "websocket",
                "certificate_pem_file",
                config.ws_certificate_pem_file.c_str());

            ini.SetValue(
                "websocket",
                "key_pem_file",
                config.ws_key_pem_file.c_str());

            ini.SetValue(
                "websocket",
                "key_pem_pass",
                config.ws_key_pem_pass.c_str());

            ini.SetValue(
                "websocket",
                "proxy_server",
                config.ws_proxy_server.c_str());

            ini.SetValue(
                "websocket",
                "protocols",
                config.ws_protocols.c_str());

            // ============================================================
            // SOCKS5
            // ============================================================
            ini.SetValue(
                "socks5",
                "bind_ip",
                config.socks5_bind_ip.c_str());

            ini.SetLongValue(
                "socks5",
                "bind_port",
                config.socks5_bind_port);

            // ============================================================
            // Server
            // ============================================================
            ini.SetValue(
                "server",
                "password",
                config.server_password.c_str());

            ini.SetLongValue(
                "server",
                "asio_thread_count",
                config.asio_thread_count);

            // ============================================================
            // 保存
            // ============================================================
            SI_Error status = ini.SaveFile(exec_dir.c_str());

            if (status != SI_OK)
            {
                PLOG_ERROR
                    << "create default config_client.ini failed, status="
                    << status;
                return;
            }

            PLOG_INFO << "default config.ini created";
        }
        else
        {
            SI_Error status = ini.LoadFile(exec_dir.c_str());

            if (status != SI_OK)
            {
                PLOG_ERROR
                    << "read config.ini failed, status="
                    << status;
                return;
            }
        }

        // ================================================================
        // STUN
        // ================================================================
        config.stun_ip =
            ini.GetValue(
                "stun",
                "ip",
                "stun.miwifi.com");

        config.stun_port =
            static_cast<int>(ini.GetLongValue(
                "stun",
                "port",
                3478));

        // ================================================================
        // WebSocket
        // ================================================================
        config.ws_server_ip =
            ini.GetValue(
                "websocket",
                "server_ip",
                "ws://127.0.0.1:80");

        config.ws_max_message_size =
            static_cast<int>(ini.GetLongValue(
                "websocket",
                "max_message_size",
                1024 * 1024));

        config.ws_connection_timeout =
            static_cast<int>(ini.GetLongValue(
                "websocket",
                "connection_timeout",
                3000));

        config.ws_enable_tls =
            ini.GetBoolValue(
                "websocket",
                "enable_tls",
                false);

        config.ws_disable_tls_verification =
            ini.GetBoolValue(
                "websocket",
                "disable_tls_verification",
                false);

        config.ws_max_outstanding_pings =
            static_cast<int>(ini.GetLongValue(
                "websocket",
                "max_outstanding_pings",
                100));

        config.ws_ping_interval =
            static_cast<int>(ini.GetLongValue(
                "websocket",
                "ping_interval",
                3000));

        config.ws_ca_certificate_pem_file =
            ini.GetValue(
                "websocket",
                "ca_certificate_pem_file",
                "");

        config.ws_certificate_pem_file =
            ini.GetValue(
                "websocket",
                "certificate_pem_file",
                "");

        config.ws_key_pem_file =
            ini.GetValue(
                "websocket",
                "key_pem_file",
                "");

        config.ws_key_pem_pass =
            ini.GetValue(
                "websocket",
                "key_pem_pass",
                "");

        config.ws_proxy_server =
            ini.GetValue(
                "websocket",
                "proxy_server",
                "");

        config.ws_protocols =
            ini.GetValue(
                "websocket",
                "protocols",
                "");

        // ================================================================
        // SOCKS5
        // ================================================================
        config.socks5_bind_ip =
            ini.GetValue(
                "socks5",
                "bind_ip",
                "0.0.0.0");

        config.socks5_bind_port =
            static_cast<int>(ini.GetLongValue(
                "socks5",
                "bind_port",
                10801));

        // ================================================================
        // Server
        // ================================================================
        config.server_password =
            ini.GetValue(
                "server",
                "password",
                "");

        config.asio_thread_count =
            static_cast<int>(ini.GetLongValue(
                "server",
                "asio_thread_count",
                4));

        PLOG_INFO << "config loaded successfully";
    }
};
