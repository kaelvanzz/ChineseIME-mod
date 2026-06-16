#pragma once

#include "common.h"
#include <windows.h>
#include <vector>
#include <string>

namespace chineseime {

InputMethodType detectInputMethodTypeFromHkl(HKL hkl);

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId);

InputMethodType detectInputMethodTypeFromGuid(REFGUID guidProfile, REFCLSID clsid, LANGID langid);

InputMethodType detectInputMethodTypeFromLayoutName(const wchar_t* klName, LANGID langId);

bool IsChineseLangId(LANGID langId);

const wchar_t* getInputMethodTypeName(InputMethodType type);

std::vector<std::wstring> collectCandidatesFromWindowEnumeration();

} // namespace chineseime