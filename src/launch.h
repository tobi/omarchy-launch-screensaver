#pragma once

#include <string>

// pidfile + flock. Also true if another process's cmdline matches
// org.omarchy.screensaver (leftover terminal-based screensaver).
bool screensaver_already_running();

// Acquire exclusive instance lock. Returns false if another instance holds it.
// The lock lives until process exit (fd leaked on purpose / held in a static).
bool acquire_instance_lock();

// omarchy-toggle-enabled screensaver-off, else the flag file.
bool screensaver_toggled_off();

// omarchy-shell lock isLocked == true. Missing helper => false.
bool session_is_locked();

void quiet_walker();

void claim_screensaver_cmdline(char *argv0);

