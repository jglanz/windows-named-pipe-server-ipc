#pragma once
#include <windows.h>
#include <string>


namespace IPC {
  std::string GetLastErrorAsString(DWORD err = GetLastError());
  HANDLE CreateManualResetEvent();
}