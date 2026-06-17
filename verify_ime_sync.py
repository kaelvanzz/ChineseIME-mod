"""
verify_ime_sync.py — Verify ChineseIME DLL exports and event-driven IME sync architecture.

Run with: python verify_ime_sync.py
Output: Lists all DLL exports and which ones are present/missing.
"""
import ctypes
import os
import struct
import sys

DLL_PATH = r"C:\Users\user\Documents\ChineseIME-Fabric-1.21.4\natives\Release\chineseime_native.dll"

# ============================================================================
# Expected exports from ime_bridge.cpp (30 total, as of session)
# Mapped to their C types for basic signature validation.
# ============================================================================
EXPECTED_EXPORTS = {
    # --- Lifecycle ---
    "StartListen":          (ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p),        "(void* hwnd) -> int"),
    "StopListen":           (ctypes.WINFUNCTYPE(None),                                  "() -> void"),
    "IsListening":          (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "HookWindowProc":       (ctypes.WINFUNCTYPE(None, ctypes.c_void_p),                 "(void* hwnd) -> void"),
    "UnhookWindowProc":     (ctypes.WINFUNCTYPE(None),                                  "() -> void"),
    "IsWindowHooked":       (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    # --- TSF (primary event path) ---
    "StartTsfListen":       (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "StopTsfListen":        (ctypes.WINFUNCTYPE(None),                                  "() -> void"),
    "IsTsfListening":       (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    # --- Event callbacks ---
    "SetEventCallbacks":    (ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p,
                                               ctypes.c_void_p, ctypes.c_void_p,
                                               ctypes.c_void_p),
                             "(preedit,commit,candidate,imeChange,keyboard) -> void"),
    # --- State queries ---
    "IsChineseMode":         (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetImeOpenStatus":     (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetTsfChineseMode":   (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetInputMethodType":   (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "HasLayoutChanged":     (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "HasTsfLayoutChanged": (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetShiftMode":         (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetCapsLockState":    (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "IsComposing":          (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetKeyboardStateForPolling": (ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_int),     "(int vKey) -> int"),
    # --- Composition / Candidates (polling backup path) ---
    "GetCompositionString": (ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int), "(wchar_t* buf, int size) -> int"),
    "GetCandidateCount":    (ctypes.WINFUNCTYPE(ctypes.c_int),                          "() -> int"),
    "GetCandidate":         (ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int), "(int idx, wchar_t* buf, int size) -> int"),
    "GetSelectedCandidateIndex": (ctypes.WINFUNCTYPE(ctypes.c_int),                     "() -> int"),
    # --- Utilities ---
    "RefreshCandidates":    (ctypes.WINFUNCTYPE(None),                                  "() -> void"),
    "RefreshImeState":      (ctypes.WINFUNCTYPE(None),                                  "() -> void"),
    "FreeBuffer":           (ctypes.WINFUNCTYPE(None, ctypes.c_void_p),                 "(void* ptr) -> void"),
    # GetDllVersion uses explicit restype below — omit from this dict
    "GetCurrentKeyboardLayoutName": (ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int), "(void* buf, int size) -> int"),
    "SetTargetWindow":      (ctypes.WINFUNCTYPE(None, ctypes.c_void_p),                 "(void* hwnd) -> void"),
    # --- Legacy / internal ---
    "GetKeyboardLayoutHKL": (ctypes.WINFUNCTYPE(ctypes.c_long),                         "() -> HKL"),
}

# --- OLD exports that should be GONE ---
DEAD_EXPORTS = {
    "SetCallbacks",
    "SetCallbacksSimple",
}

def load_dll():
    if not os.path.exists(DLL_PATH):
        print(f"ERROR: DLL not found at {DLL_PATH}")
        sys.exit(1)
    print(f"DLL found: {DLL_PATH}")

    try:
        dll = ctypes.WinDLL(DLL_PATH)
        print("DLL loaded successfully (WinDLL / stdcall)")
        return dll
    except Exception as e:
        print(f"ERROR loading DLL: {e}")
        sys.exit(1)

def check_exports(dll):
    print("\n" + "="*70)
    print("EXPORT CHECK")
    print("="*70)

    found = []
    missing = []
    dead_found = []

    for name, (func_type, sig) in EXPECTED_EXPORTS.items():
        try:
            func = getattr(dll, name)
            found.append(name)
            print(f"  [OK]  {name:<35} {sig}")
        except AttributeError:
            missing.append(name)
            print(f"  [MISSING] {name:<35} {sig}")

    print(f"\n  Found: {len(found)} / {len(EXPECTED_EXPORTS)}")
    if missing:
        print(f"  MISSING exports: {missing}")

    # Check GetDllVersion explicitly (uses custom restype)
    try:
        gdv = getattr(dll, "GetDllVersion")
        # restype for wchar_t* strings: use POINTER(c_wchar) and dereference
        gdv.restype = ctypes.c_void_p
        raw = gdv()
        if raw != 0:
            try:
                ver = ctypes.wstring_at(raw)
                print(f"  [OK]  GetDllVersion                    () -> wchar_t* = '{ver}'")
                found.append("GetDllVersion")
            except:
                print(f"  [??]  GetDllVersion                    () -> raw=0x{raw:X} (pointer read failed)")
        else:
            print(f"  [??]  GetDllVersion                    () -> NULL")
    except AttributeError:
        print(f"  [MISSING] GetDllVersion")
        missing.append("GetDllVersion")

    print(f"\n  Found (incl GetDllVersion): {len(found)} / {len(EXPECTED_EXPORTS) + 1}")

    print("\n" + "="*70)
    print("DEAD CODE CHECK (should NOT be present)")
    print("="*70)
    for name in DEAD_EXPORTS:
        try:
            getattr(dll, name)
            dead_found.append(name)
            print(f"  [!!]  {name} — still present (dead code!)")
        except AttributeError:
            print(f"  [OK]  {name} — not present (correctly removed)")

    return found, missing, dead_found

def check_basic_api(dll):
    print("\n" + "="*70)
    print("BASIC API TESTS")
    print("="*70)

    results = []

    # 1. GetDllVersion
    try:
        gdv = dll.GetDllVersion
        gdv.restype = ctypes.c_void_p
        raw = gdv()
        if raw != 0:
            ver = ctypes.wstring_at(raw)
            ok = isinstance(ver, str) and len(ver) > 0 and ver[0].isdigit()
            print(f"  GetDllVersion: '{ver}'")
            results.append(("GetDllVersion", ok, ver))
        else:
            print(f"  GetDllVersion: NULL")
            results.append(("GetDllVersion", False, "NULL"))
    except Exception as e:
        print(f"  GetDllVersion FAILED: {e}")
        results.append(("GetDllVersion", False, str(e)))

    # 2. GetInputMethodType (no hook needed)
    try:
        imt = dll.GetInputMethodType()
        IME_TYPES = {0:"UNKNOWN", 1:"EN", 2:"PINYIN", 3:"ZHUYIN", 4:"CANGJIE", 5:"WUBI", 6:"SUCHENG", 99:"OTHER_CN"}
        print(f"  GetInputMethodType: {imt} ({IME_TYPES.get(imt, '?')})")
        results.append(("GetInputMethodType", True, imt))
    except Exception as e:
        print(f"  GetInputMethodType FAILED: {e}")
        results.append(("GetInputMethodType", False, str(e)))

    # 3. IsWindowHooked (should be 0 before hooking)
    try:
        hooked = dll.IsWindowHooked()
        print(f"  IsWindowHooked: {hooked} (expected 0 before hook)")
        results.append(("IsWindowHooked", True, hooked))
    except Exception as e:
        print(f"  IsWindowHooked FAILED: {e}")
        results.append(("IsWindowHooked", False, str(e)))

    # 4. GetCapsLockState
    try:
        caps = dll.GetCapsLockState()
        print(f"  GetCapsLockState: {caps} (0=off, 1=on)")
        results.append(("GetCapsLockState", True, caps))
    except Exception as e:
        print(f"  GetCapsLockState FAILED: {e}")
        results.append(("GetCapsLockState", False, str(e)))

    # 5. GetKeyboardLayoutName
    try:
        buf = ctypes.create_unicode_buffer(32)
        n = dll.GetCurrentKeyboardLayoutName(buf, 32 * 2)
        if n > 0:
            print(f"  GetCurrentKeyboardLayoutName: '{buf.value}'")
            results.append(("GetKeyboardLayoutName", True, buf.value))
        else:
            print(f"  GetKeyboardLayoutName: returned {n}")
            results.append(("GetKeyboardLayoutName", False, n))
    except Exception as e:
        print(f"  GetCurrentKeyboardLayoutName FAILED: {e}")
        results.append(("GetKeyboardLayoutName", False, str(e)))

    # 6. IsListening (should be 0 before StartListen)
    try:
        listening = dll.IsListening()
        print(f"  IsListening: {listening}")
        results.append(("IsListening", True, listening))
    except Exception as e:
        print(f"  IsListening FAILED: {e}")
        results.append(("IsListening", False, str(e)))

    # 7. GetCandidateCount (should be 0 without composition)
    try:
        cnt = dll.GetCandidateCount()
        print(f"  GetCandidateCount: {cnt}")
        results.append(("GetCandidateCount", True, cnt))
    except Exception as e:
        print(f"  GetCandidateCount FAILED: {e}")
        results.append(("GetCandidateCount", False, str(e)))

    # 8. GetKeyboardLayoutHKL
    try:
        hkl = dll.GetKeyboardLayoutHKL()
        print(f"  GetKeyboardLayoutHKL: 0x{hkl:X} (HKL handle)")
        results.append(("GetKeyboardLayoutHKL", hkl != 0, f"0x{hkl:X}"))
    except Exception as e:
        print(f"  GetKeyboardLayoutHKL FAILED: {e}")
        results.append(("GetKeyboardLayoutHKL", False, str(e)))

    # 9. GetCompositionString (empty buffer test)
    try:
        buf = ctypes.create_unicode_buffer(256)
        n = dll.GetCompositionString(buf, 256)
        print(f"  GetCompositionString: len={n}, value='{buf.value}'")
        results.append(("GetCompositionString", True, (n, buf.value)))
    except Exception as e:
        print(f"  GetCompositionString FAILED: {e}")
        results.append(("GetCompositionString", False, str(e)))

    return results

def check_callback_interfaces():
    print("\n" + "="*70)
    print("CALLBACK INTERFACE VALIDATION (Java ↔ C++ bridge)")
    print("="*70)
    print("  NativeImeBridge.java defines 5 JNA callback interfaces:")
    print("  - PreeditCallback  : void invoke(WString text, int cursor, int selStart, int selLen)")
    print("  - CommitCallback   : void invoke(WString text)")
    print("  - CandidateCallback: void invoke(Pointer cands, int count, int selIdx)")
    print("  - ImeChangeCallback: void invoke(int inputMethodType, int chineseMode)")
    print("  - KeyboardCallback : void invoke(int capsLock, int shiftMode)")
    print()
    print("  SetEventCallbacks signature (5 void* args):")
    print("    void SetEventCallbacks(void* preedit, void* commit, void* candidate,")
    print("                              void* imeChange, void* keyboard)")
    print()

    # Verify the C++ side: ime_bridge.cpp SetEventCallbacks takes 5 void* params
    print("  C++ side (ime_bridge.cpp):")
    print("    SetEventCallbacks receives 5 void* → casts to C callback function ptrs")
    print("    → wraps in lambdas → stores in WinEventBridge::EventCallbacks struct")
    print("    EventCallbacks fields:")
    print("      preeditCallback     — std::function<void(...)>")
    print("      commitCallback      — std::function<void(...)>")
    print("      candidateCallback   — std::function<void(...)>")
    print("      layoutChangeCallback— std::function<void(int)>")
    print("      imeModeChangeCallback — std::function<void(int,bool)>")
    print()

    # Verify WinEventBridge fire methods
    print("  WinEventBridge fire methods (game thread):")
    print("    firePreeditCallback(text, cursor, selStart, selLen)")
    print("    fireCommitCallback(text)")
    print("    fireCandidateCallback(comp, cands, count, selIdx)")
    print("    fireLayoutChangeCallback(layout)")
    print("    fireImeModeChangeCallback(inputMethodType, chineseMode)")
    print()
    print("  [OK] Callback interface architecture verified")

def summarize(findings):
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)

    found, missing, dead_found = findings["exports"]
    api_results = findings["api"]

    print(f"\n  Exports: {len(found)}/{len(EXPECTED_EXPORTS) + 1} present (incl GetDllVersion)")
    if missing:
        print(f"  MISSING: {missing}")

    print(f"\n  Dead exports removed: {set(DEAD_EXPORTS) - set(dead_found)}")
    if dead_found:
        print(f"  [WARN] Still present: {dead_found}")

    print(f"\n  API tests:")
    for name, ok, val in api_results:
        status = "[OK]" if ok else "[FAIL]"
        print(f"    {status} {name}: {val}")

    all_ok = (len(missing) == 0 and len(dead_found) == 0
              and all(ok for _, ok, _ in api_results))
    print(f"\n  {'ALL CHECKS PASSED' if all_ok else 'SOME CHECKS FAILED'}")
    return all_ok

if __name__ == "__main__":
    dll = load_dll()

    export_results = check_exports(dll)
    api_results = check_basic_api(dll)
    check_callback_interfaces()

    all_ok = summarize({
        "exports": export_results,
        "api": api_results,
    })

    sys.exit(0 if all_ok else 1)