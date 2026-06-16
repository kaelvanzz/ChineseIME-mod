# Fcitx5 Candidate Bridge 详解

## 源码位置

原始实现: `/home/kryo/Downloads/fcitx5_candidate_bridge.cpp`

## 工作原理

### 1. DBus 连接

```cpp
DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
```

连接到当前用户的 D-Bus session 总线。

### 2. 监听信号

```cpp
const char* matchRule =
    "type='signal',"
    "interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged'";

dbus_bus_add_match(conn, matchRule, &err);
```

监听所有 `PropertiesChanged` 信号（包含 Fcitx5 候选词变更）。

### 3. 消息循环

```cpp
while (true) {
    dbus_connection_read_write(conn, 200);  // 200ms 超时
    DBusMessage* msg = dbus_connection_pop_message(conn);
    if (msg == nullptr) continue;

    handleSignal(msg, port);
    dbus_message_unref(msg);
}
```

### 4. 候选词解析

Fcitx5 通过 `org.freedesktop.DBus.Properties.PropertiesChanged` 信号发送更新：

```cpp
void handleSignal(DBusMessage* msg, int targetPort) {
    // 解析信号参数
    DBusMessageIter args, dict, entry, variant, arr;

    // 遍历属性字典
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        // 提取属性名
        const char* key;
        dbus_message_iter_get_basic(&entry, &key);

        // CandidateList 属性
        if (propName == "CandidateList") {
            // 解析候选词数组
            while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID) {
                // 提取每个候选词
                const char* text;
                dbus_message_iter_get_basic(&st, &text);
                candidates.push_back(text);
            }
        }

        // Preedit 属性
        if (propName == "Preedit" || propName == "ClientPreedit") {
            dbus_message_iter_get_basic(&variant, &p);
            preedit = p;
        }
    }

    // 发送 JSON 到 TCP 端口
    sendToLocalPort(json, targetPort);
}
```

### 5. TCP 转发

```cpp
bool sendToLocalPort(const std::string& json, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    connect(sock, ...);
    std::string payload = json + "\n";
    send(sock, payload.c_str(), payload.size(), 0);
    close(sock);
}
```

每个消息以换行符结尾，便于 Java 端按行读取。

---

## 数据格式

### JSON 输出

```json
{
    "preedit": "ni",
    "highlighted": 0,
    "candidates": ["你", "尼", "呢", "妮", "泥"]
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `preedit` | string | 当前输入的预编辑文本 |
| `highlighted` | int | 高亮的候选词索引（0-based），-1 表示无 |
| `candidates` | array | 候选词列表 |

---

## Fcitx5 DBus API

### 关键接口

- `org.fcitx.Fcitx.InputContext` - 输入上下文
- `org.fcitx.Fcitx` - 主接口

### 常用属性

| 属性 | 类型 | 说明 |
|------|------|------|
| `Preedit` | `s` | 当前预编辑字符串 |
| `ClientPreedit` | `s` | 客户端预编辑（可能不同） |
| `CandidateList` | `as` 或 `a(sb)` | 候选词列表（字符串或带高亮标记） |

### 相关 DBus 方法

```bash
# 查看 Fcitx5 服务
dbus-send --session --print-reply --dest=org.fcitx.Fcitx \
    /org/fcitx/Fcitx org.fcitx.Fcitx.GetCurrentIM

# 监听所有属性变更
dbus-monitor "type='signal',interface='org.freedesktop.DBus.Properties'"
```

---

## 扩展建议

### 1. 添加更多属性

```cpp
} else if (propName == "InputMethod") {
    // 当前输入法名称
} else if (propName == "Layout") {
    // 键盘布局
}
```

### 2. 支持 Unix Socket

```cpp
#include <sys/un.h>

int sock = socket(AF_UNIX, SOCK_STREAM, 0);
sockaddr_un addr{};
addr.sun_family = AF_UNIX;
strcpy(addr.sun_path, "/tmp/fcitx_bridge.sock");
connect(sock, (sockaddr*)&addr, sizeof(addr));
```

### 3. 添加日志

```cpp
#include <spdlog/spdlog.h>

spdlog::info("Sending {} candidates", candidates.size());
spdlog::debug("JSON: {}", json);
```

---

## 已知限制

1. **只支持 session bus** - 不支持 system bus
2. **依赖 Fcitx5** - 不支持 IBus
3. **需要 X11/Wayland** - Fcitx5 必须在运行中
4. **候选词格式可能变化** - 不同输入法候选词格式不同