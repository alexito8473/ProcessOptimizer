
#include "ProcessIcon.h"

#include <Shellapi.h>

#pragma comment(lib, "Shell32.lib")

HICON ProcessIcon::GetIcon(const wchar_t* executablePath)
{
    if (!executablePath || executablePath[0] == L'\0')
        return nullptr;

    SHFILEINFOW fileInfo{};

    const DWORD_PTR result = SHGetFileInfoW(
        executablePath,
        0,
        &fileInfo,
        sizeof(fileInfo),
        SHGFI_ICON | SHGFI_LARGEICON
    );

    if (result == 0)
        return nullptr;

    return fileInfo.hIcon;
}
