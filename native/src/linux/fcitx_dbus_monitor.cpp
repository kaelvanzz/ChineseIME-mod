#include "fcitx_dbus_monitor.h"
#include "common_linux.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[ChineseIME-Fcitx] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

namespace chineseime {

namespace {

bool dbus_error_check(DBusError* error, const char* context) {
    if (dbus_error_is_set(error)) {
        DEBUG_LOG("DBus error at %s: %s", context, error->message);
        dbus_error_free(error);
        return false;
    }
    return true;
}

std::string get_string_property(DBusConnection* conn, const char* path,
                                 const char* iface, const char* prop) {
    DBusMessage* msg = dbus_message_new_method_call(
        "org.fcitx.Fcitx.InputContext",
        path,
        "org.freedesktop.DBus.Properties",
        "Get");

    if (!msg) return "";

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 100, &error);

    std::string result;
    if (reply) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter)) {
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
                DBusMessageIter variant;
                dbus_message_iter_recurse(&iter, &variant);
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                    char* val = nullptr;
                    dbus_message_iter_get_basic(&variant, &val);
                    if (val) result = val;
                }
            }
        }
        dbus_message_unref(reply);
    }
    dbus_message_unref(msg);
    return result;
}

int get_int_property(DBusConnection* conn, const char* path,
                     const char* iface, const char* prop) {
    DBusMessage* msg = dbus_message_new_method_call(
        "org.fcitx.Fcitx.InputContext",
        path,
        "org.freedesktop.DBus.Properties",
        "Get");

    if (!msg) return 0;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 100, &error);

    int result = 0;
    if (reply) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter)) {
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
                DBusMessageIter variant;
                dbus_message_iter_recurse(&iter, &variant);
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_INT32) {
                    dbus_message_iter_get_basic(&variant, &result);
                }
            }
        }
        dbus_message_unref(reply);
    }
    dbus_message_unref(msg);
    return result;
}

std::vector<std::wstring> get_candidates(DBusConnection* conn, const char* path) {
    std::vector<std::wstring> result;

    DBusMessage* msg = dbus_message_new_method_call(
        "org.fcitx.Fcitx.InputContext",
        path,
        "org.fcitx.Fcitx.InputContext",
        "GetCandidates");

    if (!msg) return result;

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 100, &error);

    if (reply) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter)) {
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
                DBusMessageIter array;
                dbus_message_iter_recurse(&iter, &array);
                while (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_INVALID) {
                    if (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
                        DBusMessageIter dict;
                        dbus_message_iter_recurse(&array, &dict);
                        if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRING) {
                            char* str = nullptr;
                            dbus_message_iter_get_basic(&dict, &str);
                            if (str) {
                                result.emplace_back();
                                for (int i = 0; str[i]; ++i) {
                                    result.back() += (wchar_t)str[i];
                                }
                            }
                        }
                    }
                    dbus_message_iter_next(&array);
                }
            }
        }
        dbus_message_unref(reply);
    }
    dbus_message_unref(msg);
    return result;
}

std::string get_current_ic_path(DBusConnection* conn) {
    DBusMessage* msg = dbus_message_new_method_call(
        "org.fcitx.Fcitx",
        "/org/fcitx/Fcitx",
        "org.fcitx.Fcitx",
        "CurrentInputMethod");

    if (!msg) return "";

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 500, &error);

    std::string result;
    if (reply) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH) {
            char* path = nullptr;
            dbus_message_iter_get_basic(&iter, &path);
            if (path) result = path;
        }
        dbus_message_unref(reply);
    }
    dbus_message_unref(msg);
    return result;
}

} // anonymous namespace

FcitxDBusMonitor::FcitxDBusMonitor() {
}

FcitxDBusMonitor::~FcitxDBusMonitor() {
    shutdown();
}

bool FcitxDBusMonitor::initialize() {
    DBusError error;
    dbus_error_init(&error);

    conn_ = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!conn_ || dbus_error_check(&error, "dbus_bus_get")) {
        DEBUG_LOG("Failed to connect to D-Bus session");
        return false;
    }

    dbus_bus_request_name(conn_, "com.example.ChineseIME", 0, &error);
    if (dbus_error_check(&error, "dbus_bus_request_name")) {
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }

    connected_.store(true);
    running_.store(true);
    DEBUG_LOG("Fcitx DBus monitor initialized");
    return true;
}

void FcitxDBusMonitor::shutdown() {
    if (!running_.load()) return;
    running_.store(false);

    if (conn_) {
        dbus_connection_unref(conn_);
        conn_ = nullptr;
    }
    connected_.store(false);
}

FcitxDBusMonitor::ICStatus FcitxDBusMonitor::getCurrentICStatus() {
    ICStatus status;
    if (!conn_) return status;

    std::string icPath = get_current_ic_path(conn_);
    if (icPath.empty()) return status;

    status.icid = icPath;
    status.enabled = get_int_property(conn_, icPath.c_str(),
        "org.fcitx.Fcitx.InputContext", "Enabled") == 1;
    status.engine = get_string_property(conn_, icPath.c_str(),
        "org.fcitx.Fcitx.InputContext", "Properties");

    DEBUG_LOG("IC Status: path=%s, enabled=%d", icPath.c_str(), status.enabled);
    return status;
}

FcitxDBusMonitor::IMState FcitxDBusMonitor::getInputState(const std::string& icid) {
    IMState state;
    if (!conn_ || icid.empty()) return state;

    state.enabled = get_int_property(conn_, icid.c_str(),
        "org.fcitx.Fcitx.InputContext", "Enabled") == 1;

    state.candidates = get_candidates(conn_, icid.c_str());

    state.selectedIndex = get_int_property(conn_, icid.c_str(),
        "org.fcitx.Fcitx.InputContext", "CursorPos");

    state.composition = L"";
    std::string comp = get_string_property(conn_, icid.c_str(),
        "org.fcitx.Fcitx.InputContext", "Preedit");
    for (char c : comp) {
        state.composition += (wchar_t)c;
    }

    state.inputMethod = get_string_property(conn_, icid.c_str(),
        "org.fcitx.Fcitx.InputContext", "CurrentInputMethod");

    return state;
}

void FcitxDBusMonitor::poll() {
    if (!connected_.load() || !conn_) return;

    dbus_connection_read_write(conn_, 0);
    while (dbus_connection_dispatch(conn_) == DBUS_DISPATCH_DATA_REMAINS) {
    }

    auto icStatus = getCurrentICStatus();
    if (icStatus.icid.empty()) return;

    auto imState = getInputState(icStatus.icid);

    std::lock_guard<std::mutex> lock(mutex_);

    ImeStateManager::get().updateImeOpen(imState.enabled);
    ImeStateManager::get().updateChineseMode(imState.enabled);

    ImeStateManager::get().updateCandidates(
        imState.composition,
        imState.candidates,
        imState.selectedIndex
    );

    DEBUG_LOG("Poll: enabled=%d, comp='%S', cands=%zu",
        imState.enabled, imState.composition.c_str(), imState.candidates.size());
}

} // namespace chineseime