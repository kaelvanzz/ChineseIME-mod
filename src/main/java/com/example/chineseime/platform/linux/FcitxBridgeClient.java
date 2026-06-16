package com.example.chineseime.platform.linux;

import com.example.chineseime.ChineseIMEInitializer;
import net.minecraft.client.MinecraftClient;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.Socket;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

public class FcitxBridgeClient {
    private static final int DEFAULT_PORT = 6767;
    private static final int RECONNECT_DELAY_MS = 2000;
    private static final int BUFFER_SIZE = 4096;

    public interface CandidateListener {
        void onCandidates(String preedit, List<String> candidates, int highlighted);
        void onConnected();
        void onDisconnected();
        void onError(String message);
    }

    private final int port;
    private final CandidateListener listener;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private Thread readerThread;
    private Socket socket;
    private BufferedReader reader;

    public FcitxBridgeClient(int port, CandidateListener listener) {
        this.port = port;
        this.listener = listener;
    }

    public FcitxBridgeClient(CandidateListener listener) {
        this(DEFAULT_PORT, listener);
    }

    public void start() {
        if (running.compareAndSet(false, true)) {
            readerThread = new Thread(this::runLoop, "FcitxBridgeReader");
            readerThread.setDaemon(true);
            readerThread.start();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME-Fcitx] Bridge client started, connecting to port {}", port);
        }
    }

    public void stop() {
        if (running.compareAndSet(true, false)) {
            disconnect();
            if (readerThread != null) {
                readerThread.interrupt();
                try {
                    readerThread.join(1000);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
                readerThread = null;
            }
            ChineseIMEInitializer.LOGGER.info("[ChineseIME-Fcitx] Bridge client stopped");
        }
    }

    public boolean isConnected() {
        return connected.get();
    }

    private void runLoop() {
        while (running.get()) {
            try {
                if (!connect()) {
                    Thread.sleep(RECONNECT_DELAY_MS);
                    continue;
                }

                String line;
                while (running.get() && (line = reader.readLine()) != null) {
                    if (!line.isEmpty()) {
                        processLine(line);
                    }
                }
            } catch (InterruptedException e) {
                break;
            } catch (Exception e) {
                if (running.get()) {
                    ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Fcitx] Connection error: {}", e.getMessage());
                    disconnected();
                }
            }

            if (running.get()) {
                try {
                    Thread.sleep(RECONNECT_DELAY_MS);
                } catch (InterruptedException e) {
                    break;
                }
            }
        }
    }

    private boolean connect() {
        disconnect();

        try {
            socket = new Socket("127.0.0.1", port);
            socket.setSoTimeout(0);
            socket.setKeepAlive(true);
            reader = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8), BUFFER_SIZE);
            connected.set(true);

            ChineseIMEInitializer.LOGGER.info("[ChineseIME-Fcitx] Connected to bridge");
            if (listener != null) {
                listener.onConnected();
            }
            return true;
        } catch (UnknownHostException e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Fcitx] Unknown host: {}", e.getMessage());
            if (listener != null) {
                listener.onError("Unknown host: " + e.getMessage());
            }
        } catch (IOException e) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME-Fcitx] Connection failed: {}", e.getMessage());
            if (listener != null) {
                listener.onError("Connection failed: " + e.getMessage());
            }
        }
        return false;
    }

    private void disconnect() {
        connected.set(false);

        if (reader != null) {
            try {
                reader.close();
            } catch (IOException ignored) {}
            reader = null;
        }

        if (socket != null) {
            try {
                socket.close();
            } catch (IOException ignored) {}
            socket = null;
        }
    }

    private void disconnected() {
        connected.set(false);
        if (listener != null) {
            listener.onDisconnected();
        }
    }

    private void processLine(String line) {
        try {
            String preedit = parseJsonString(line, "preedit");
            int highlighted = parseJsonInt(line, "highlighted");
            List<String> candidates = parseJsonArray(line, "candidates");

            ChineseIMEInitializer.LOGGER.debug("[ChineseIME-Fcitx] Received: preedit='{}', {} candidates, highlighted={}",
                preedit, candidates.size(), highlighted);

            if (listener != null) {
                listener.onCandidates(preedit, candidates, highlighted);
            }
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Fcitx] Failed to parse JSON: {} - '{}'",
                e.getMessage(), line);
        }
    }

    private String parseJsonString(String json, String key) {
        String pattern = "\"" + key + "\":\"";
        int start = json.indexOf(pattern);
        if (start == -1) {
            pattern = "\"" + key + "\" :\"";
            start = json.indexOf(pattern);
        }
        if (start == -1) return "";

        start += pattern.length();
        int end = start;
        while (end < json.length()) {
            char c = json.charAt(end);
            if (c == '\\' && end + 1 < json.length()) {
                end += 2;
            } else if (c == '"') {
                break;
            } else {
                end++;
            }
        }
        return json.substring(start, end);
    }

    private int parseJsonInt(String json, String key) {
        String pattern = "\"" + key + "\":";
        int start = json.indexOf(pattern);
        if (start == -1) {
            pattern = "\"" + key + "\" :";
            start = json.indexOf(pattern);
        }
        if (start == -1) return -1;

        start += pattern.length();
        while (start < json.length() && Character.isWhitespace(json.charAt(start))) start++;

        int end = start;
        while (end < json.length() && (Character.isDigit(json.charAt(end)) || json.charAt(end) == '-')) {
            end++;
        }
        if (end == start) return -1;

        try {
            return Integer.parseInt(json.substring(start, end));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private List<String> parseJsonArray(String json, String key) {
        List<String> result = new ArrayList<>();
        String pattern = "\"" + key + "\":[";
        int arrStart = json.indexOf(pattern);
        if (arrStart == -1) {
            pattern = "\"" + key + "\" :[";
            arrStart = json.indexOf(pattern);
        }
        if (arrStart == -1) return result;

        arrStart += pattern.length() - 1;
        int bracketCount = 1;
        int i = arrStart + 1;
        List<Integer> itemStarts = new ArrayList<>();
        List<Boolean> inString = new ArrayList<>();

        while (i < json.length() && bracketCount > 0) {
            char c = json.charAt(i);
            if (c == '"' && (i == 0 || json.charAt(i - 1) != '\\')) {
                if (!inString.isEmpty() && inString.get(inString.size() - 1)) {
                    inString.remove(inString.size() - 1);
                } else {
                    inString.add(true);
                }
            } else if (!inString.isEmpty() && inString.get(inString.size() - 1)) {
            } else if (c == '[' || c == '{') {
                bracketCount++;
            } else if (c == ']' || c == '}') {
                bracketCount--;
                if (bracketCount == 1 && c == ']') {
                    itemStarts.add(i + 1);
                }
            } else if (c == ',' && bracketCount == 1) {
                itemStarts.add(i + 1);
            }
            i++;
        }

        for (int j = 0; j < itemStarts.size() - (bracketCount == 0 ? 0 : 1); j++) {
            String item = json.substring(itemStarts.get(j), itemStarts.get(j + 1) - 1).trim();
            if (item.startsWith("\"")) {
                item = item.substring(1);
            }
            if (item.endsWith("\"")) {
                item = item.substring(0, item.length() - 1);
            }
            item = unescapeJsonString(item);
            result.add(item);
        }

        return result;
    }

    private String unescapeJsonString(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) == '\\' && i + 1 < s.length()) {
                char next = s.charAt(i + 1);
                switch (next) {
                    case '"': sb.append('"'); i++; break;
                    case '\\': sb.append('\\'); i++; break;
                    case 'n': sb.append('\n'); i++; break;
                    case 't': sb.append('\t'); i++; break;
                    default: sb.append(s.charAt(i)); break;
                }
            } else {
                sb.append(s.charAt(i));
            }
        }
        return sb.toString();
    }
}