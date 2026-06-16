# ChineseIME Linux 平台开发文档

## 架构概览

Linux 平台采用**独立进程 + Socket 通信**架构，与 Windows 的 C++ JNA 方案不同：

```
┌──────────────────────────────────────────────────┐
│           Minecraft (Fabric Mod)                 │
│  ┌────────────────────────────────────────────┐  │
│  │  LinuxIMEEventBridge.java                  │  │
│  │  └── FcitxBridgeClient.java (TCP Socket)   │  │
│  └────────────────────────────────────────────┘  │
└──────────────────────┬───────────────────────────┘
                      │ Socket (127.0.0.1:6767)
                      │ JSON over TCP
                      ▼
┌──────────────────────────────────────────────────┐
│  fcitx5_candidate_bridge (独立进程)               │
│  - DBus 监听 Fcitx5                              │
│  - 解析候选词信号                                  │
│  - JSON 序列化 → TCP 推送                         │
└──────────────────────────────────────────────────┘
```

### 优势
1. **解耦** - bridge 出错不影响 Minecraft
2. **跨语言** - Java 直接 socket 读 JSON，无需 JNA/C++ 交叉编译
3. **独立调试** - bridge 可以单独运行测试
4. **简化 native** - 不需要复杂 DBus 处理

---

## 目录结构

```
ChineseIME-mod/
├── documents/linux/
│   ├── README.md              # 本文档
│   ├── BUILD.md                # 编译指南
│   ├── FCITX_BRIDGE.md         # Fcitx bridge 详情
│   └── DEBUG.md                # 调试指南
├── src/main/java/com/example/chineseime/platform/linux/
│   ├── FcitxBridgeClient.java  # TCP 客户端
│   ├── LinuxIMEEventBridge.java
│   └── NativeLinuxBridge.java  # (保留，备用)
└── native/
    └── build_linux/           # (保留，备用 native 方案)
```

---

## 快速开始

### 1. 编译 bridge

```bash
# 安装依赖
sudo pacman -S dbus libdbus-glib  # Arch
sudo apt install libdbus-1-dev     # Debian/Ubuntu
sudo dnf install dbus-devel        # Fedora

# 编译
g++ /path/to/fcitx5_candidate_bridge.cpp -o fcitx5_candidate_bridge \
  `pkg-config --cflags --libs dbus-1` -lpthread

# 运行
./fcitx5_candidate_bridge 6767 &
```

### 2. 启动 Minecraft

确保 Mod 已正确安装，启动后在日志中应看到：
```
[ChineseIME-Fcitx] Bridge client started, connecting to port 6767
[ChineseIME-Fcitx] Connected to bridge
```

---

## FcitxBridgeClient

### 核心接口

```java
public interface CandidateListener {
    void onCandidates(String preedit, List<String> candidates, int highlighted);
    void onConnected();
    void onDisconnected();
    void onError(String message);
}
```

### 使用方式

```java
FcitxBridgeClient client = new FcitxBridgeClient(6767, new CandidateListener() {
    @Override
    public void onCandidates(String preedit, List<String> candidates, int highlighted) {
        // 更新 HUD
        horizontalHud.updateCandidatesKeepSelection(candidates, preedit, highlighted, 0);
    }

    @Override
    public void onConnected() {
        // 连接成功
    }

    @Override
    public void onDisconnected() {
        // 断开连接，尝试重连
    }

    @Override
    public void onError(String message) {
        // 日志记录
    }
});

client.start();
// 业务逻辑
client.stop();
```

### JSON 格式

bridge 发送的 JSON 格式：
```json
{
    "preedit": "ni",
    "highlighted": 0,
    "candidates": ["你", "尼", "呢", "妮", "泥"]
}
```

---

## fcitx5_candidate_bridge

### 编译

```bash
g++ fcitx5_candidate_bridge.cpp -o bridge \
  `pkg-config --cflags --libs dbus-1` -lpthread -Wall
```

### 运行参数

```bash
./bridge [port]  # 默认 6767
```

### DBus 信号

bridge 监听 Fcitx5 的 `org.freedesktop.DBus.Properties.PropertiesChanged` 信号，解析 `CandidateList` 和 `Preedit` 属性。

### 调试

```bash
# 查看 DBus 信号
dbus-monitor "type='signal',interface='org.freedesktop.DBus.Properties'"

# 查看端口连接
netstat -tlnp | grep 6767
```

---

## 编译 Mod

在 Windows 机器上：

```powershell
./gradlew.bat build
```

产物：
- `build/libs/chineseime-1.0.0.jar` - Mod JAR
- 需要附带 `fcitx5_candidate_bridge` 可执行文件

### 部署

```powershell
# 复制 Mod JAR
copy build\libs\chineseime-1.0.0.jar "mods/"

# Linux 端需要同时部署 bridge
scp fcitx5_candidate_bridge user@game-server:/path/to/mods/
```

---

## 备选方案：原生 C++ 实现

如果不需要独立进程，可以编译 native `.so`：

```bash
cd native/build_linux
cmake . -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 输出
natives/linux/libchineseime_native_linux.so
```

然后修改 `LinuxIMEEventBridge` 使用 `NativeLinuxBridge` 而非 `FcitxBridgeClient`。

---

## 调试技巧

### Mod 日志
```
[ChineseIME-Fcitx] Bridge client started
[ChineseIME-Fcitx] Connected to bridge
[ChineseIME-Fcitx] Received: preedit='ni', 9 candidates, highlighted=0
```

### Bridge 调试
```cpp
// 在 handleSignal() 中添加
std::cout << "DBus signal received" << std::endl;
```

### 网络调试
```bash
# 监听端口
nc -l 6767

# 或用 socat
socat - TCP-LISTEN:6767,reuseaddr
```

---

## 常见问题

### Q: bridge 连接失败
A: 检查 bridge 是否运行 `pgrep fcitx5_candidate_bridge`，检查端口 `netstat -tlnp | grep 6767`

### Q: 候选词不显示
A: 检查 Mod 日志中是否有 `onCandidates` 调用，检查 JSON 是否正确解析

### Q: DBus 信号未收到
A: 确认 Fcitx5 是当前输入法，确认 DBus session 正确 `echo $DBUS_SESSION_BUS_ADDRESS`

### Q: 延迟过高
A: 检查 bridge 的 `dbus_connection_read_write()` 超时参数，调整轮询间隔

---

## 未来优化

1. **支持多端口** - 同时监听多个输入法
2. **Unix Socket** - 更高效的本地通信
3. **Wayland 支持** - 兼容 Wayland 下的 Fcitx5
4. **Rime 支持** - librime 集成作为备选输入法
