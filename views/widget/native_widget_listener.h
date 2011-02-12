// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_WIDGET_NATIVE_WIDGET_LISTENER_H_
#define VIEWS_WIDGET_NATIVE_WIDGET_LISTENER_H_

#include "ui/gfx/native_widget_types.h"

namespace gfx {
class Canvas;
class Point;
class Size;
}

namespace views {
class KeyEvent;
class MouseEvent;
class MouseWheelEvent;
class WidgetImpl;

namespace internal {

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetListener interface
//
//  An interface implemented by the Widget that handles events sent from a
//  NativeWidget implementation.
//
class NativeWidgetListener {
 public:
  virtual ~NativeWidgetListener() {}

  virtual void OnClose() = 0;

  virtual void OnDestroy() = 0;
  virtual void OnDisplayChanged() = 0;

  virtual bool OnKeyEvent(const KeyEvent& event) = 0;

  virtual void OnMouseCaptureLost() = 0;

  virtual bool OnMouseEvent(const MouseEvent& event) = 0;
  virtual bool OnMouseWheelEvent(const MouseWheelEvent& event) = 0;

  virtual void OnNativeWidgetCreated() = 0;

  virtual void OnPaint(gfx::Canvas* canvas) = 0;
  virtual void OnSizeChanged(const gfx::Size& size) = 0;

  virtual void OnNativeFocus(gfx::NativeView focused_view) = 0;
  virtual void OnNativeBlur(gfx::NativeView focused_view) = 0;

  virtual void OnWorkAreaChanged() = 0;

  virtual WidgetImpl* GetWidgetImpl() const = 0;
};

}  // namespace internal
}  // namespace views

#endif  // VIEWS_WIDGET_NATIVE_WIDGET_LISTENER_H_
