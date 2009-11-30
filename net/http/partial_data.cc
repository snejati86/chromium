// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/partial_data.h"

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"

namespace {

// The headers that we have to process.
const char kLengthHeader[] = "Content-Length";
const char kRangeHeader[] = "Content-Range";
const int kDataStream = 1;

}

namespace net {

bool PartialData::Init(const std::string& headers) {
  std::vector<HttpByteRange> ranges;
  if (!HttpUtil::ParseRanges(headers, &ranges) || ranges.size() != 1)
    return false;

  // We can handle this range request.
  byte_range_ = ranges[0];
  if (!byte_range_.IsValid())
    return false;

  resource_size_ = 0;
  current_range_start_ = byte_range_.first_byte_position();
  return true;
}

void PartialData::SetHeaders(const std::string& headers) {
  DCHECK(extra_headers_.empty());
  extra_headers_ = headers;
}

void PartialData::RestoreHeaders(std::string* headers) const {
  DCHECK(current_range_start_ >= 0 || byte_range_.IsSuffixByteRange());
  int64 end = byte_range_.IsSuffixByteRange() ?
              byte_range_.suffix_length() : byte_range_.last_byte_position();

  AddRangeHeader(current_range_start_, end, headers);
}

int PartialData::PrepareCacheValidation(disk_cache::Entry* entry,
                                        std::string* headers) {
  DCHECK(current_range_start_ >= 0);

  // Scan the disk cache for the first cached portion within this range.
  int64 range_len = byte_range_.HasLastBytePosition() ?
      byte_range_.last_byte_position() - current_range_start_ + 1 : kint32max;
  if (range_len > kint32max)
    range_len = kint32max;
  int len = static_cast<int32>(range_len);
  if (!len)
    return 0;
  range_present_ = false;

  if (sparse_entry_) {
    cached_min_len_ = entry->GetAvailableRange(current_range_start_, len,
                                               &cached_start_);
  } else if (truncated_) {
    if (!current_range_start_) {
      // Update the cached range only the first time.
      cached_min_len_ = static_cast<int32>(byte_range_.first_byte_position());
      cached_start_ = 0;
    }
  } else {
    cached_min_len_ = len;
    cached_start_ = current_range_start_;
  }

  if (cached_min_len_ < 0) {
    DCHECK(cached_min_len_ != ERR_IO_PENDING);
    return cached_min_len_;
  }

  headers->assign(extra_headers_);

  if (!cached_min_len_) {
    // We don't have anything else stored.
    final_range_ = true;
    cached_start_ =
        byte_range_.HasLastBytePosition() ? current_range_start_  + len : 0;
  }

  if (current_range_start_ == cached_start_) {
    // The data lives in the cache.
    range_present_ = true;
    if (len == cached_min_len_)
      final_range_ = true;
    AddRangeHeader(current_range_start_, cached_start_ + cached_min_len_ - 1,
                   headers);
  } else {
    // This range is not in the cache.
    AddRangeHeader(current_range_start_, cached_start_ - 1, headers);
  }

  // Return a positive number to indicate success (versus error or finished).
  return 1;
}

bool PartialData::IsCurrentRangeCached() const {
  return range_present_;
}

bool PartialData::IsLastRange() const {
  return final_range_;
}

bool PartialData::UpdateFromStoredHeaders(const HttpResponseHeaders* headers,
                                          disk_cache::Entry* entry,
                                          bool truncated) {
  resource_size_ = 0;
  if (truncated) {
    DCHECK_EQ(headers->response_code(), 200);
    // We don't have the real length and the user may be trying to create a
    // sparse entry so let's not write to this entry.
    if (byte_range_.IsValid())
      return false;

    truncated_ = true;
    sparse_entry_ = false;
    byte_range_.set_first_byte_position(entry->GetDataSize(kDataStream));
    current_range_start_ = 0;
    return true;
  }

  if (headers->response_code() == 200) {
    DCHECK(byte_range_.IsValid());
    sparse_entry_ = false;
    resource_size_ = entry->GetDataSize(kDataStream);
    return true;
  }

  std::string length_value;
  if (!headers->GetNormalizedHeader(kLengthHeader, &length_value))
    return false;  // We must have stored the resource length.

  if (!StringToInt64(length_value, &resource_size_) || !resource_size_)
    return false;

  // Make sure that this is really a sparse entry.
  int64 n;
  if (ERR_CACHE_OPERATION_NOT_SUPPORTED == entry->GetAvailableRange(0, 5, &n))
    return false;

  return true;
}

bool PartialData::IsRequestedRangeOK() {
  if (byte_range_.IsValid()) {
    if (truncated_)
      return true;
    if (!byte_range_.ComputeBounds(resource_size_))
      return false;

    if (current_range_start_ < 0)
      current_range_start_ = byte_range_.first_byte_position();
  } else {
    // This is not a range request but we have partial data stored.
    current_range_start_ = 0;
    byte_range_.set_last_byte_position(resource_size_ - 1);
  }

  bool rv = current_range_start_ >= 0;
  if (!rv)
    current_range_start_ = 0;

  return rv;
}

bool PartialData::ResponseHeadersOK(const HttpResponseHeaders* headers) {
  if (headers->response_code() == 304) {
    if (!byte_range_.IsValid() || truncated_)
      return true;

    // We must have a complete range here.
    return byte_range_.HasFirstBytePosition() &&
           byte_range_.HasLastBytePosition();
  }

  int64 start, end, total_length;
  if (!headers->GetContentRange(&start, &end, &total_length))
    return false;
  if (total_length <= 0)
    return false;

  int64 content_length = headers->GetContentLength();
  if (content_length < 0 || content_length != end - start + 1)
    return false;

  if (!resource_size_) {
    // First response. Update our values with the ones provided by the server.
    resource_size_ = total_length;
    if (!byte_range_.HasFirstBytePosition()) {
      byte_range_.set_first_byte_position(start);
      current_range_start_ = start;
    }
    if (!byte_range_.HasLastBytePosition())
      byte_range_.set_last_byte_position(end);
  } else if (resource_size_ != total_length) {
    return false;
  }

  if (start != current_range_start_)
    return false;

  if (byte_range_.IsValid() && end > byte_range_.last_byte_position())
    return false;

  return true;
}

// We are making multiple requests to complete the range requested by the user.
// Just assume that everything is fine and say that we are returning what was
// requested.
void PartialData::FixResponseHeaders(HttpResponseHeaders* headers) {
  if (truncated_)
    return;

  headers->RemoveHeader(kLengthHeader);
  headers->RemoveHeader(kRangeHeader);

  int64 range_len;
  if (byte_range_.IsValid()) {
    if (!sparse_entry_)
      headers->ReplaceStatusLine("HTTP/1.1 206 Partial Content");

    DCHECK(byte_range_.HasFirstBytePosition());
    DCHECK(byte_range_.HasLastBytePosition());
    headers->AddHeader(
        StringPrintf("%s: bytes %" PRId64 "-%" PRId64 "/%" PRId64,
                     kRangeHeader,
                     byte_range_.first_byte_position(),
                     byte_range_.last_byte_position(),
                     resource_size_));
    range_len = byte_range_.last_byte_position() -
                byte_range_.first_byte_position() + 1;
  } else {
    // TODO(rvargas): Is it safe to change the protocol version?
    headers->ReplaceStatusLine("HTTP/1.1 200 OK");
    DCHECK_NE(resource_size_, 0);
    range_len = resource_size_;
  }

  headers->AddHeader(StringPrintf("%s: %" PRId64, kLengthHeader, range_len));
}

void PartialData::FixContentLength(HttpResponseHeaders* headers) {
  headers->RemoveHeader(kLengthHeader);
  headers->AddHeader(StringPrintf("%s: %" PRId64, kLengthHeader,
                                  resource_size_));
}

int PartialData::CacheRead(disk_cache::Entry* entry, IOBuffer* data,
                           int data_len, CompletionCallback* callback) {
  int read_len = std::min(data_len, cached_min_len_);
  if (!read_len)
    return 0;

  int rv = 0;
  if (sparse_entry_) {
    rv = entry->ReadSparseData(current_range_start_, data, read_len,
                               callback);
  } else {
    if (current_range_start_ > kint32max)
      return ERR_INVALID_ARGUMENT;

    rv = entry->ReadData(kDataStream, static_cast<int>(current_range_start_),
                         data, read_len, callback);
  }
  return rv;
}

int PartialData::CacheWrite(disk_cache::Entry* entry, IOBuffer* data,
                           int data_len, CompletionCallback* callback) {
  if (sparse_entry_) {
    return entry->WriteSparseData(current_range_start_, data, data_len,
                                  callback);
  } else  {
    if (current_range_start_ > kint32max)
      return ERR_INVALID_ARGUMENT;

    return entry->WriteData(kDataStream, static_cast<int>(current_range_start_),
                            data, data_len, callback, true);
  }
}

void PartialData::OnCacheReadCompleted(int result) {
  if (result > 0) {
    current_range_start_ += result;
    cached_min_len_ -= result;
    DCHECK(cached_min_len_ >= 0);
  }
}

void PartialData::OnNetworkReadCompleted(int result) {
  if (result > 0)
    current_range_start_ += result;
}

// Static.
void PartialData::AddRangeHeader(int64 start, int64 end, std::string* headers) {
  DCHECK(start >= 0 || end >= 0);
  std::string my_start, my_end;
  if (start >= 0)
    my_start = Int64ToString(start);
  if (end >= 0)
    my_end = Int64ToString(end);

  headers->append(StringPrintf("Range: bytes=%s-%s\r\n", my_start.c_str(),
                               my_end.c_str()));
}

}  // namespace net
