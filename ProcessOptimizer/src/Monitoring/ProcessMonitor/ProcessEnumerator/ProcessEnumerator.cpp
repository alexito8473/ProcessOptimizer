


#include <filesystem>

#include "../ProcessInfo.h"
#include "ProcessEnumerator.h"
#include <TlHelp32.h>

ProcessSnapshot ProcessEnumerator::Enumerate()
{
    ProcessSnapshot snapshot;

    HANDLE snapshotHandle = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS,
        0
    );

    if (snapshotHandle == INVALID_HANDLE_VALUE)
        return snapshot;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshotHandle, &entry))
    {
        do
        {
            ProcessInfo process;

            process.processId = entry.th32ProcessID;
            process.parentProcessId = entry.th32ParentProcessID;

            process.name = entry.szExeFile;
            process.executableName = entry.szExeFile;

            HANDLE processHandle = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                entry.th32ProcessID
            );

            if (processHandle)
            {
                wchar_t path[MAX_PATH]{};
                DWORD pathSize = MAX_PATH;

                if (QueryFullProcessImageNameW(
                    processHandle,
                    0,
                    path,
                    &pathSize))
                {
                    process.executablePath.assign(path, pathSize);

                    try
                    {
                        process.executableName =
                            std::filesystem::path(
                                process.executablePath
                            ).filename().wstring();
                    }
                    catch (...)
                    {
                    }
                }

                CloseHandle(processHandle);
            }

            snapshot.processes.push_back(std::move(process));

        } while (Process32NextW(snapshotHandle, &entry));
    }

    CloseHandle(snapshotHandle);

    return snapshot;
}
