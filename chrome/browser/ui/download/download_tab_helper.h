// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_TAB_HELPER_H_
#define CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_TAB_HELPER_H_
#pragma once

#include "base/basictypes.h"
#include "chrome/browser/download/save_package.h"

// Per-tab download controller. Handles dealing with various per-tab download
// duties.
class DownloadTabHelper {
 public:
  explicit DownloadTabHelper(TabContents* tab_contents);
  virtual ~DownloadTabHelper();

  // Prepare for saving the current web page to disk.
  void OnSavePage();

  // Save page with the main HTML file path, the directory for saving resources,
  // and the save type: HTML only or complete web page. Returns true if the
  // saving process has been initiated successfully.
  bool SavePage(const FilePath& main_file, const FilePath& dir_path,
                SavePackage::SavePackageType save_type);

  // Returns the SavePackage which manages the page saving job. May be NULL.
  SavePackage* save_package() const { return save_package_.get(); }

 private:
  TabContents* tab_contents_;

  // SavePackage, lazily created.
  scoped_refptr<SavePackage> save_package_;

  DISALLOW_COPY_AND_ASSIGN(DownloadTabHelper);
};

#endif  // CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_TAB_HELPER_H_
