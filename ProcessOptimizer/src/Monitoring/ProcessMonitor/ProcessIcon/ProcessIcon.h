
#pragma once

#include <Windows.h>

class ProcessIcon
{
public:
    static HICON GetIcon(const wchar_t* executablePath);
};
