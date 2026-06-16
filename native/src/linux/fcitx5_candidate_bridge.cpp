#include <dbus/dbus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstring>

// 将内容格式化为json
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

std::string buildCandidateJson(const std::vector<std::string>& candidates,
                                const std::string& preedit,
                                int highlighted) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"preedit\":\"" << jsonEscape(preedit) << "\",";
    oss << "\"highlighted\":" << highlighted << ",";
    oss << "\"candidates\":[";
    for (size_t i = 0; i < candidates.size(); ++i) {
        oss << "\"" << jsonEscape(candidates[i]) << "\"";
        if (i + 1 < candidates.size()) oss << ",";
    }
    oss << "]}";
    return oss.str();
}

// tcp这一块
bool sendToLocalPort(const std::string& json, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    std::string payload = json + "\n";
    send(sock, payload.c_str(), payload.size(), 0);
    close(sock);
    return true;
}

// fcitx5候选词解析
void handleSignal(DBusMessage* msg, int targetPort) {
    if (!dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged"))
        return;

    DBusMessageIter args, dict, entry, variant, arr;
    if (!dbus_message_iter_init(msg, &args)) return;
   
    dbus_message_iter_next(&args);

    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return;
    dbus_message_iter_recurse(&args, &dict);

    std::vector<std::string> candidates;
    std::string preedit;
    int highlighted = -1;

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&dict, &entry);

        const char* key;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        dbus_message_iter_recurse(&entry, &variant);

        std::string propName(key);

        if (propName == "CandidateList") {
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&variant, &arr);
                int idx = 0;
                while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID) {
                    if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
                        DBusMessageIter st;
                        dbus_message_iter_recurse(&arr, &st);
                        const char* text;
                        dbus_message_iter_get_basic(&st, &text);
                        candidates.push_back(text);

                        dbus_message_iter_next(&st);
                        if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_BOOLEAN) {
                            dbus_bool_t hl;
                            dbus_message_iter_get_basic(&st, &hl);
                            if (hl) highlighted = idx;
                        }
                    } else if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
                        const char* text;
                        dbus_message_iter_get_basic(&arr, &text);
                        candidates.push_back(text);
                    }
                    dbus_message_iter_next(&arr);
                    idx++;
                }
            }
        } else if (propName == "Preedit" || propName == "ClientPreedit") {
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                const char* p;
                dbus_message_iter_get_basic(&variant, &p);
                preedit = p;
            }
        }

        dbus_message_iter_next(&dict);
    }

    if (!candidates.empty() || !preedit.empty()) {
        std::string json = buildCandidateJson(candidates, preedit, highlighted);
        std::cout << "Sending: " << json << std::endl;
        sendToLocalPort(json, targetPort);
    }
}

int main(int argc, char** argv) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 9999;

    DBusError err;
    dbus_error_init(&err);

    // 连接 session bus
    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        std::cerr << "DBus connection error: " << err.message << std::endl;
        dbus_error_free(&err);
        return 1;
    }

    // 监听fcitx5
    const char* matchRule =
        "type='signal',"
        "interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged'";

    dbus_bus_add_match(conn, matchRule, &err);
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        std::cerr << "Match error: " << err.message << std::endl;
        dbus_error_free(&err);
        return 1;
    }

    std::cout << "Listening for fcitx5 "
              << port << " ..." << std::endl;

    while (true) {
        dbus_connection_read_write(conn, 200);
        DBusMessage* msg = dbus_connection_pop_message(conn);
        if (msg == nullptr) continue;

        handleSignal(msg, port);
        dbus_message_unref(msg);
    }

    dbus_connection_unref(conn);
    return 0;
}