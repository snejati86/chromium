// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system_key_event_listener.h"

#define XK_MISCELLANY 1
#include <X11/keysymdef.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#undef Status

#include "base/message_pump_x.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/input_method/xkeyboard.h"
#include "ui/base/x/x11_util.h"

namespace chromeos {

namespace {
static SystemKeyEventListener* g_system_key_event_listener = NULL;
}  // namespace

// static
void SystemKeyEventListener::Initialize() {
  CHECK(!g_system_key_event_listener);
  g_system_key_event_listener = new SystemKeyEventListener();
}

// static
void SystemKeyEventListener::Shutdown() {
  // We may call Shutdown without calling Initialize, e.g. if we exit early.
  if (g_system_key_event_listener) {
    delete g_system_key_event_listener;
    g_system_key_event_listener = NULL;
  }
}

// static
SystemKeyEventListener* SystemKeyEventListener::GetInstance() {
  VLOG_IF(1, !g_system_key_event_listener)
      << "SystemKeyEventListener::GetInstance() with NULL global instance.";
  return g_system_key_event_listener;
}

SystemKeyEventListener::SystemKeyEventListener()
    : stopped_(false),
      num_lock_mask_(0),
      xkb_event_base_(0) {
  input_method::XKeyboard* xkeyboard =
      input_method::InputMethodManager::GetInstance()->GetXKeyboard();
  num_lock_mask_ = xkeyboard->GetNumLockMask();
  xkeyboard->GetLockedModifiers(&caps_lock_is_on_, &num_lock_is_on_);

  Display* display = ui::GetXDisplay();
  int xkb_major_version = XkbMajorVersion;
  int xkb_minor_version = XkbMinorVersion;
  if (!XkbQueryExtension(display,
                         NULL,  // opcode_return
                         &xkb_event_base_,
                         NULL,  // error_return
                         &xkb_major_version,
                         &xkb_minor_version)) {
    LOG(WARNING) << "Could not query Xkb extension";
  }

  if (!XkbSelectEvents(display, XkbUseCoreKbd,
                       XkbStateNotifyMask,
                       XkbStateNotifyMask)) {
    LOG(WARNING) << "Could not install Xkb Indicator observer";
  }

  MessageLoopForUI::current()->AddObserver(this);
}

SystemKeyEventListener::~SystemKeyEventListener() {
  Stop();
}

void SystemKeyEventListener::Stop() {
  if (stopped_)
    return;
  MessageLoopForUI::current()->RemoveObserver(this);
  stopped_ = true;
}

void SystemKeyEventListener::AddCapsLockObserver(CapsLockObserver* observer) {
  caps_lock_observers_.AddObserver(observer);
}

void SystemKeyEventListener::RemoveCapsLockObserver(
    CapsLockObserver* observer) {
  caps_lock_observers_.RemoveObserver(observer);
}

base::EventStatus SystemKeyEventListener::WillProcessEvent(
    const base::NativeEvent& event) {
  return ProcessedXEvent(event) ? base::EVENT_HANDLED : base::EVENT_CONTINUE;
}

void SystemKeyEventListener::DidProcessEvent(const base::NativeEvent& event) {
}

void SystemKeyEventListener::OnCapsLock(bool enabled) {
  FOR_EACH_OBSERVER(CapsLockObserver,
                    caps_lock_observers_,
                    OnCapsLockChange(enabled));
}

bool SystemKeyEventListener::ProcessedXEvent(XEvent* xevent) {
  input_method::InputMethodManager* input_method_manager =
      input_method::InputMethodManager::GetInstance();

  if (xevent->type == xkb_event_base_) {
    // TODO(yusukes): Move this part to aura::RootWindowHost.
    XkbEvent* xkey_event = reinterpret_cast<XkbEvent*>(xevent);
    if (xkey_event->any.xkb_type == XkbStateNotify) {
      input_method::ModifierLockStatus new_caps_lock_state =
          input_method::kDontChange;
      input_method::ModifierLockStatus new_num_lock_state =
          input_method::kDontChange;

      bool enabled = (xkey_event->state.locked_mods) & LockMask;
      if (caps_lock_is_on_ != enabled) {
        caps_lock_is_on_ = enabled;
        new_caps_lock_state =
            enabled ? input_method::kEnableLock : input_method::kDisableLock;
        OnCapsLock(caps_lock_is_on_);
      }

      enabled = (xkey_event->state.locked_mods) & num_lock_mask_;
      if (num_lock_is_on_ != enabled) {
        num_lock_is_on_ = enabled;
        if (num_lock_is_on_) {
          // TODO(yusukes,adlr): Let the user know that num lock is unsupported.
        }
        // Force turning off Num Lock. crosbug.com/29169
        new_num_lock_state = input_method::kDisableLock;
      }

      // Propagate the keyboard LED change to _ALL_ keyboards
      input_method_manager->GetXKeyboard()->SetLockedModifiers(
          new_caps_lock_state, new_num_lock_state);

      return true;
    }
  }
  return false;
}

}  // namespace chromeos
