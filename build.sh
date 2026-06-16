#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOD_DIR="$SCRIPT_DIR"
NATIVE_DIR="$MOD_DIR/native"
JAVA_HOME_PATH=""

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --java-home PATH    Set JAVA_HOME explicitly"
    echo "  --stop              Stop Gradle daemon and exit"
    echo "  -h, --help          Show this help"
    echo ""
    echo "Examples:"
    echo "  $0                              # Use default Java"
    echo "  $0 --java-home /path/to/jdk21   # Use specific JDK"
    echo "  $0 --stop                       # Stop daemon"
}

STOP_DAEMON=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --java-home)
            JAVA_HOME_PATH="$2"
            shift 2
            ;;
        --stop)
            STOP_DAEMON=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if [ "$STOP_DAEMON" = true ]; then
    if [ -n "$JAVA_HOME_PATH" ]; then
        export JAVA_HOME="$JAVA_HOME_PATH"
    fi
    cd "$MOD_DIR"
    ./gradlew --stop
    exit 0
fi

if [ -n "$JAVA_HOME_PATH" ]; then
    export JAVA_HOME="$JAVA_HOME_PATH"
elif [ -d "/usr/lib/jvm/java-21-openjdk" ]; then
    export JAVA_HOME="/usr/lib/jvm/java-21-openjdk"
fi

if [ -z "$JAVA_HOME" ]; then
    echo "ERROR: JAVA_HOME not set and no default JDK found."
    echo "Please install JDK 21 or set --java-home"
    exit 1
fi

echo "Using JAVA_HOME: $JAVA_HOME"
echo ""

cd "$MOD_DIR"

echo "=========================================="
echo "Building ChineseIME Native (.so)..."
echo "=========================================="
cd "$NATIVE_DIR"
bash build_linux.sh

echo ""
echo "=========================================="
echo "Building ChineseIME Mod (.jar)..."
echo "=========================================="
cd "$MOD_DIR"

./gradlew build --parallel --max-workers=16

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo ""
echo "Output files:"
ls -lh "$MOD_DIR/build/libs/"*.jar 2>/dev/null || echo "  No jar found in build/libs/"
ls -lh "$MOD_DIR/natives/Linux/"*.so 2>/dev/null || echo "  No .so found in natives/Linux/"
echo ""
echo "To install, copy the jar to your Minecraft mods folder."