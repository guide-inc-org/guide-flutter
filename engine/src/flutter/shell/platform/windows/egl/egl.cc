// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/egl/egl.h"

#include <EGL/egl.h>
#include <windows.h>
#include <atomic>
#include <sstream>
#include <excpt.h>

#include "flutter/fml/logging.h"

namespace flutter {
namespace egl {

namespace {

const wchar_t* kGraphicsErrorTitle = L"HYPER SBI FX";

// Callback function to enumerate and close all windows of current process
BOOL CALLBACK CloseWindowCallback(HWND hwnd, LPARAM lParam) {
  __try {
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);

    // If this window belongs to our process, close it
    if (process_id == GetCurrentProcessId()) {
      // Check if window is valid and visible
      if (IsWindow(hwnd)) {
        FML_LOG(ERROR) << "Closing window: " << hwnd;

        // Use PostMessage instead of SendMessage to avoid blocking
        // If PostMessage fails, try destroying the window directly
        if (!PostMessage(hwnd, WM_CLOSE, 0, 0)) {
          FML_LOG(ERROR) << "PostMessage failed, attempting DestroyWindow...";
          DestroyWindow(hwnd);
        }
      }
    }
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {
    FML_LOG(ERROR) << "Exception in CloseWindowCallback, continuing...";
  }

  return TRUE;  // Continue enumeration even if an error occurs
}

const char* EGLErrorToString(EGLint error) {
  switch (error) {
    case EGL_SUCCESS:
      return "Success";
    case EGL_NOT_INITIALIZED:
      return "Not Initialized";
    case EGL_BAD_ACCESS:
      return "Bad Access";
    case EGL_BAD_ALLOC:
      return "Bad Alloc";
    case EGL_BAD_ATTRIBUTE:
      return "Bad Attribute";
    case EGL_BAD_CONTEXT:
      return "Bad Context";
    case EGL_BAD_CONFIG:
      return "Bad Config";
    case EGL_BAD_CURRENT_SURFACE:
      return "Bad Current Surface";
    case EGL_BAD_DISPLAY:
      return "Bad Display";
    case EGL_BAD_SURFACE:
      return "Bad Surface";
    case EGL_BAD_MATCH:
      return "Bad Match";
    case EGL_BAD_PARAMETER:
      return "Bad Parameter";
    case EGL_BAD_NATIVE_PIXMAP:
      return "Bad Native Pixmap";
    case EGL_BAD_NATIVE_WINDOW:
      return "Bad Native Window";
    case EGL_CONTEXT_LOST:
      return "Context Lost";
  }
  FML_UNREACHABLE();
  return "Unknown";
}

}  // namespace

void LogEGLError(std::string_view message) {
  const EGLint error = ::eglGetError();
  FML_LOG(ERROR) << "EGL Error: " << EGLErrorToString(error) << " ("
                 << error << ") " << message;

  // Static flag to prevent multiple dialog instances
  static std::atomic<bool> dialog_shown{false};

  // Show dialog for context lost errors
  if (error == EGL_CONTEXT_LOST) {
    // Check if dialog is already shown
    bool expected = false;
    if (!dialog_shown.compare_exchange_strong(expected, true)) {
      // Dialog is already being shown by another thread, just return
      // The thread showing the dialog will handle the exit
      return;
    }

    // Log that we're about to show the dialog
    FML_LOG(ERROR) << "Showing EGL context lost dialog...";

    std::wstringstream msg;
    msg << L"エラーが発生しました。\n\n";
    msg << L"OKボタンを押して、HYPER SBI FXを終了してください。";

    // Try multiple times to ensure dialog is shown
    bool dialog_shown_successfully = false;

    __try {
      // First attempt: Try with foreground window
      HWND parent_window = NULL;
      __try {
        parent_window = GetForegroundWindow();
      }
      __except(EXCEPTION_EXECUTE_HANDLER) {
        parent_window = NULL;
      }

      if (parent_window != NULL) {
        int result = MessageBoxW(parent_window, msg.str().c_str(),
                    kGraphicsErrorTitle,
                    MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
        if (result != 0) {
          dialog_shown_successfully = true;
        }
      }

      // Second attempt: Try without parent window if first failed
      if (!dialog_shown_successfully) {
        FML_LOG(ERROR) << "First MessageBox attempt failed, trying without parent...";
        int result = MessageBoxW(NULL, msg.str().c_str(),
                    kGraphicsErrorTitle,
                    MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
        if (result != 0) {
          dialog_shown_successfully = true;
        }
      }

      // Third attempt: Try with desktop window
      if (!dialog_shown_successfully) {
        FML_LOG(ERROR) << "Second MessageBox attempt failed, trying with desktop window...";
        HWND desktop = GetDesktopWindow();
        int result = MessageBoxW(desktop, msg.str().c_str(),
                    kGraphicsErrorTitle,
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        if (result != 0) {
          dialog_shown_successfully = true;
        }
      }

      // Final attempt: Force topmost system modal
      if (!dialog_shown_successfully) {
        FML_LOG(ERROR) << "All attempts failed, forcing topmost dialog...";
        MessageBoxW(NULL, msg.str().c_str(),
                   kGraphicsErrorTitle,
                   MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_TOPMOST | MB_DEFAULT_DESKTOP_ONLY);
      }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
      FML_LOG(ERROR) << "Critical exception in MessageBox handling! Forcing exit...";
      // Even if everything fails, we must exit
    }

    // Attempt graceful shutdown with timeout protection
    FML_LOG(ERROR) << "Attempting graceful shutdown...";

    __try {
      // Step 1: Enumerate and close all windows belonging to this process
      FML_LOG(ERROR) << "Enumerating and closing all windows of current process...";

      // Use a try-catch to protect against EnumWindows failures
      BOOL enum_result = EnumWindows(CloseWindowCallback, 0);
      if (!enum_result) {
        FML_LOG(ERROR) << "EnumWindows failed or was interrupted";
      }

      // Give reasonable time for WM_CLOSE messages to be processed
      // But not too long to avoid hanging
      Sleep(300);

      // Step 2: Post quit message to the message loop
      FML_LOG(ERROR) << "Posting quit message...";
      PostQuitMessage(1);

      // Brief wait for message loop to process quit
      Sleep(100);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
      FML_LOG(ERROR) << "Exception during graceful shutdown, proceeding to force termination...";
    }

    // Step 3: If graceful shutdown didn't work, terminate the process
    // This ensures the app ALWAYS exits even if:
    // - Message loop is blocked
    // - Windows are hung
    // - Any unexpected exception occurred
    FML_LOG(ERROR) << "Forcing process termination as final fallback...";

    // Flush any pending log messages before termination
    fflush(stderr);
    fflush(stdout);

    // Force terminate - this WILL exit the process
    TerminateProcess(GetCurrentProcess(), 1);
  }
}

void LogEGLError(std::string_view file, int line) {
  std::stringstream stream;
  stream << "in " << file << ":" << line;
  LogEGLError(stream.str());
}

}  // namespace egl
}  // namespace flutter
