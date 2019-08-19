// Copyright (c) 2018-2019 Marco Wang <m.aesophor@gmail.com>
extern "C" {
#include <errno.h>
#include <unistd.h>
}
#include <iostream>
#include <cstring>
#include <memory>

#if GLOG_FOUND
#include <glog/logging.h>
#endif
#include "config.h"
#include "snapshot.h"
#include "stacktrace.h"
#include "window_manager.h"
#include "util.h"

namespace {

const char* version() {
  return WIN_MGR_NAME " " VERSION "\n"
    "Copyright (C) 2018-2019 Marco Wang <m.aesophor@gmail.com>\n"
    "This is free software, see the source for copying conditions. There is No\n"
    "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE";
}

const char* wm_start_failed_msg = "Failed to open display to X server.";
const char* wm_execl_failed_msg = "execl() failed";

} // namespace 


int main(int argc, char* args[]) {
  if (argc > 1 && (!std::strcmp(args[1], "-v") || !std::strcmp(args[1], "--version"))) {
    std::cout << ::version() << std::endl;
    return EXIT_SUCCESS;
  }

  // Install segv handler which writes stacktrace to a log upon segfault.
  // See stacktrace.cc
  wmderland::segv::InstallHandler(&wmderland::segv::Handle);

  // Initialize google's c++ logging library (if installed)
  // Logging-related macros are defined in config.h.in
  WM_INIT_LOGGING(args[0]);

  // WindowManager is a singleton class. If XOpenDisplay() fails during 
  // WindowManager::GetInstance(), it will return None (in Xlib, 'None'
  // is the universal null resource ID or atom.)
  std::unique_ptr<wmderland::WindowManager> wm(wmderland::WindowManager::GetInstance());

  if (!wm) {
    WM_LOG(INFO, ::wm_start_failed_msg);
    std::cerr << ::wm_start_failed_msg << std::endl;
    return EXIT_FAILURE;
  }


  try {
    // Try to perform error recovery from the snapshot if necessary and possible.
    if (wm->snapshot().FileExists()) {
      wm->snapshot().Load();
    }
    wm->Run(); // enter main event loop

  } catch (const std::bad_alloc& ex) {
    fputs("Out of memory\n", stderr);
    return EXIT_FAILURE;

  } catch (const wmderland::Snapshot::SnapshotLoadError& ex) {
    // If we cannot recover from errors using the snapshot,
    // then return with EXIT_FAILURE.
    WM_LOG(ERROR, ex.what());
    std::cerr << ex.what() << std::endl;
    rename(wm->snapshot().filename().c_str(), (wm->snapshot().filename() + ".failed_to_load").c_str());
    return EXIT_FAILURE;

  } catch (const std::exception& ex) {
    // Try to exec itself and recover from errors using the snapshot.
    // If snapshot fails to load, it will throw an SnapshotLoadError.
    // See the previous catch block.
    WM_LOG(ERROR, ex.what());
    wmderland::sys_utils::NotifySend("An error occurred. Recovering...", NOTIFY_SEND_CRITICAL);
    wm->snapshot().Save();
    
    if (execl(args[0], args[0], nullptr) == -1) {
      WM_LOG(ERROR, ::wm_execl_failed_msg << ": " << strerror(errno));
      perror(::wm_execl_failed_msg);
      return EXIT_FAILURE;
    }

  } catch (...) {
    WM_LOG(ERROR, "Unknown exception caught!");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
