#pragma once

#include <windows.h>

#include <functional>

namespace nativeapi::detail {

/**
 * @file ui_thread_windows.h
 * @brief Reaching the thread that pumps this process's Win32 messages.
 *
 * "The platform thread" on Windows is the thread that owns the process's windows
 * and runs the message loop their messages are dispatched from. It is not
 * necessarily the thread that loaded the library: a Flutter app built with the
 * default thread policy loads its plugins and native assets from the UI isolate,
 * which has no message loop of its own. Win32 refuses to deliver out-of-context
 * WinEvents there — they are posted to the installing thread's queue and never
 * read — and a window procedure only ever runs on the thread that owns the
 * window, so platform work that has to happen "inside the window" belongs on the
 * owning thread.
 *
 * These helpers marshal such work there without requiring the caller to know
 * which thread that is, which is what makes the public API usable from any
 * thread.
 */

/**
 * @brief The thread that owns this process's windows, i.e. the one Win32 dispatches
 * their messages to.
 *
 * Cached while that window lives. 0 when the process has no window of its own yet
 * (a console tool before its first window, a test): there is nothing Win32 could
 * deliver anywhere then.
 */
DWORD FindUiThreadId();

/**
 * @brief Queue @p task to run on @p thread_id.
 *
 * Always asynchronous, and safe to call from @p thread_id itself. The thread
 * gets a private message-only queue window the first time it is targeted; when
 * the caller cannot create it there (a different thread), a one-shot
 * WH_GETMESSAGE hook does, on that thread's next message.
 *
 * @return false when the thread cannot be reached — it never pumped a message,
 *         or it is gone. The task is then NOT queued.
 */
bool PostToThread(DWORD thread_id, std::function<void()> task);

}  // namespace nativeapi::detail
