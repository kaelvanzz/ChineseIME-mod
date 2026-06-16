# Linux 平台调试指南

## 调试模式

### 1. 启用 Mod 日志

在 `config/s cloth Config` 或直接修改配置：
```properties
# 设置日志级别为 debug
log.level=debug
```

或运行时添加 JVM 参数：
```bash
-Dchineseime.debug=true
```

### 2. 查看 Mod 日志

```bash
# Minecraft 日志位置
tail -f ~/.local/share/PrismLauncher/instances/*/minecraft/logs/latest.log | grep ChineseIME
```

### 3. Bridge 日志

在 `fcitx5_candidate_bridge.cpp` 中添加调试输出：
```cpp
std::cerr << "DEBUG: " << message << std::endl;
```

然后重新编译：
```bash
g++ -DDEBUG fcitx5_candidate_bridge.cpp -o fcitx5_candidate_bridge \
  `pkg-config --cflags --libs dbus-1` -lpthread
```

---

## 常见问题排查

### 问题：bridge 连接失败

**检查步骤：**

```bash
# 1. bridge 是否运行？
pgrep fcitx5_candidate_bridge

# 2. 端口是否监听？
ss -tlnp | grep 6767

# 3. 防火墙？
sudo firewall-cmd --list-ports | grep 6767
sudo iptables -L -n | grep 6767

# 4. 测试连接
nc -zv 127.0.0.1 6767
```

**解决方案：**

```bash
# 启动 bridge 并后台运行
./fcitx5_candidate_bridge 6767 &
disown

# 或使用 systemd 用户服务
```

---

### 问题：候选词不显示

**检查步骤：**

```bash
# 1. Mod 日志
grep -E "(ChineseIME-Fcitx|onCandidates)" ~/.local/share/PrismLauncher/logs/*.log

# 2. bridge 是否收到 DBus 信号？
dbus-monitor "type='signal'" | grep PropertiesChanged

# 3. JSON 是否正确发送？
nc -l 6767  # 手动接收测试
```

**解决方案：**

确保 bridge 输出的 JSON 格式正确：
```bash
# 在另一个终端运行
./fcitx5_candidate_bridge 6767 2>&1 | while read line; do
    echo "Received: $line"
done
```

---

### 问题：DBus 信号未收到

**检查步骤：**

```bash
# 1. Fcitx5 是否运行？
ps aux | grep fcitx

# 2. DBus session 是否正确？
echo $DBUS_SESSION_BUS_ADDRESS
# 应该类似: unix:path=/run/user/1000/bus

# 3. 监听 DBus 信号
dbus-monitor "type='signal',interface='org.freedesktop.DBus.Properties'"
# 然后在另一个终端触发输入法
```

**解决方案：**

```bash
# 确保 DBus session 总线可用
eval $(dbus-launch)
export DBUS_SESSION_BUS_ADDRESS

# 重启 Fcitx5
fcitx5 -r &
```

---

## 网络调试

### tcpdump

```bash
# 监听本地端口
sudo tcpdump -i lo -nn port 6767

# 保存完整抓包
sudo tcpdump -i lo -nn -w /tmp/fcitx_capture.pcap port 6767

# 分析
wireshark /tmp/fcitx_capture.pcap
```

### ss/netstat

```bash
# 查看端口状态
ss -tlnp | grep 6767

# 查看连接
ss -tp | grep 6767
```

### nc 测试

```bash
# 服务器端
nc -l 6767

# 客户端（游戏外）
echo '{"preedit":"test","highlighted":0,"candidates":["测试1","测试2"]}' | nc 127.0.0.1 6767
```

---

## 代码调试

### 添加调试日志

在 `FcitxBridgeClient.java`:

```java
private void processLine(String line) {
    ChineseIMEInitializer.LOGGER.debug("[Fcitx] Raw: {}", line);  // 添加这行
    // ...
}
```

### 断点调试

如果使用 IntelliJ IDEA：

1. 在 `FcitxBridgeClient.processLine()` 设置断点
2. 以调试模式启动 Minecraft
3. 在游戏内切换输入法触发断点

### 单元测试

```java
@Test
public void testJsonParsing() {
    FcitxBridgeClient client = new FcitxBridgeClient(6767, new CandidateListener() {
        @Override
        public void onCandidates(String preedit, List<String> candidates, int highlighted) {
            assertEquals("ni", preedit);
            assertEquals(5, candidates.size());
            assertEquals(0, highlighted);
        }
    });

    String testJson = "{\"preedit\":\"ni\",\"highlighted\":0,\"candidates\":[\"你\",\"尼\",\"呢\",\"妮\",\"泥\"]}";
    client.processLine(testJson);
}
```

---

## 性能分析

### 延迟测量

```java
long start = System.nanoTime();
// 处理
long elapsed = (System.nanoTime() - start) / 1_000_000; // ms
ChineseIMEInitializer.LOGGER.info("Processing took {} ms", elapsed);
```

### 内存分析

```bash
# 查找内存泄漏
valgrind --leak-check=full ./fcitx5_candidate_bridge

# CPU 分析
perf record -g ./fcitx5_candidate_bridge
perf report
```

---

## 参考命令速查

```bash
# 查看 bridge 进程
pgrep -af fcitx5_candidate_bridge

# 杀死 bridge
pkill fcitx5_candidate_bridge

# 查看端口占用
lsof -i :6767

# 清理所有 dbus 连接
dbus-send --session --type=method_call --dest=org.freedesktop.DBus \
    /org/freedesktop/DBus org.freedesktop.DBus.ListNames

# 重启 Fcitx5
fcitx5 -r; pkill -f fcitx5_candidate_bridge

# 完整重启
pkill fcitx5; pkill fcitx5_candidate_bridge
fcitx5 &
./fcitx5_candidate_bridge 6767 &
```