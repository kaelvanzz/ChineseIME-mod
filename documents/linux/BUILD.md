# Linux 平台编译指南

## 编译 fcitx5_candidate_bridge

### 依赖

**Arch Linux:**
```bash
sudo pacman -S base-devel dbus
```

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libdbus-1-dev
```

**Fedora:**
```bash
sudo dnf install gcc-c++ dbus-devel
```

### 编译步骤

```bash
# 克隆或获取源代码
cd /path/to/source

# 编译
g++ fcitx5_candidate_bridge.cpp -o fcitx5_candidate_bridge \
  `pkg-config --cflags --libs dbus-1` -lpthread -Wall -O2

# 测试运行
./fcitx5_candidate_bridge 6767
```

### 验证

```bash
# 检查进程
pgrep fcitx5_candidate_bridge

# 检查端口
ss -tlnp | grep 6767
```

---

## 交叉编译 (可选)

如果你在 Windows 上交叉编译 Linux 版本：

### 使用 mingw-w64 (Linux)

```bash
# Arch
sudo pacman -S mingw-w64-gcc

# 编译
x86_64-w64-mingw32-g++ fcitx5_candidate_bridge.cpp -o fcitx5_candidate_bridge.exe \
  `pkg-config --cflags --libs dbus-1` -lpthread -static
```

### 使用 Docker

```dockerfile
FROM archlinux:latest
RUN pacman -Syu --noconfirm base-devel dbus
COPY fcitx5_candidate_bridge.cpp /build/
RUN g++ /build/fcitx5_candidate_bridge.cpp -o /build/fcitx5_candidate_bridge \
  `pkg-config --cflags --libs dbus-1` -lpthread
```

---

## Mod 编译 (Windows)

```powershell
# 在 Windows 上运行
./gradlew.bat build

# 产物
build\libs\chineseime-1.0.0.jar
```

---

## 部署清单

| 文件 | 位置 | 说明 |
|------|------|------|
| `chineseime-1.0.0.jar` | `minecraft/mods/` | Mod JAR |
| `fcitx5_candidate_bridge` | `minecraft/mods/` | Bridge 可执行文件 |
| `librime.so` (可选) | 系统库 | Rime 引擎 |

### 部署脚本

```bash
#!/bin/bash
# deploy.sh

MOD_DIR="$HOME/.local/share/PrismLauncher/instances/1.21.4-fabric/minecraft/mods"
BRIDGE_DIR="$MOD_DIR/bridge"

# 复制 bridge
cp fcitx5_candidate_bridge "$BRIDGE_DIR"
chmod +x "$BRIDGE_DIR"

# 启动时运行 bridge
"$BRIDGE_DIR" 6767 &
```