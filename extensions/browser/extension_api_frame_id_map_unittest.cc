// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/run_loop.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "ipc/ipc_message.h"
#include "testing/gtest/include/gtest/gtest.h"

using FrameIdCallback = extensions::ExtensionApiFrameIdMap::FrameIdCallback;

namespace extensions {

namespace {

int ToTestFrameId(int render_process_id, int frame_routing_id) {
  if (render_process_id < 0 && frame_routing_id < 0)
    return ExtensionApiFrameIdMap::kInvalidFrameId;
  // Return a deterministic value (yet different from the input) for testing.
  // To make debugging easier: Ending with 0 = frame ID.
  return render_process_id * 1000 + frame_routing_id * 10;
}

int ToTestParentFrameId(int render_process_id, int frame_routing_id) {
  if (render_process_id < 0 && frame_routing_id < 0)
    return ExtensionApiFrameIdMap::kInvalidFrameId;
  // Return a deterministic value (yet different from the input) for testing.
  // To make debugging easier: Ending with 7 = parent frame ID.
  return render_process_id * 1000 + frame_routing_id * 10 + 7;
}

class TestExtensionApiFrameIdMap : public ExtensionApiFrameIdMap {
 public:
  int GetInternalSize() { return frame_id_map_.size(); }
  int GetInternalCallbackCount() {
    int count = 0;
    for (auto& it : callbacks_map_)
      count += it.second.callbacks.size();
    return count;
  }

  // These indirections are used because we cannot mock RenderFrameHost with
  // fixed IDs in unit tests.
  // TODO(robwu): Use content/public/test/test_renderer_host.h to mock
  // RenderFrameHosts and update the tests to test against these mocks.
  // After doing that, there is no need for CacheFrameId/RemoveFrameId methods
  // that take a RenderFrameIdKey, so the methods can be merged.
  void SetInternalFrameId(int render_process_id, int frame_routing_id) {
    CacheFrameId(RenderFrameIdKey(render_process_id, frame_routing_id));
  }
  void RemoveInternalFrameId(int render_process_id, int frame_routing_id) {
    RemoveFrameId(RenderFrameIdKey(render_process_id, frame_routing_id));
  }

 private:
  // ExtensionApiFrameIdMap:
  CachedFrameIdPair KeyToValue(const RenderFrameIdKey& key) const override {
    return CachedFrameIdPair(
        ToTestFrameId(key.render_process_id, key.frame_routing_id),
        ToTestParentFrameId(key.render_process_id, key.frame_routing_id));
  }
};

class ExtensionApiFrameIdMapTest : public testing::Test {
 public:
  ExtensionApiFrameIdMapTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP) {}

  FrameIdCallback CreateCallback(int render_process_id,
                                 int frame_routing_id,
                                 const std::string& callback_name_for_testing) {
    return base::Bind(&ExtensionApiFrameIdMapTest::OnCalledCallback,
                      base::Unretained(this), render_process_id,
                      frame_routing_id, callback_name_for_testing);
  }

  void OnCalledCallback(int render_process_id,
                        int frame_routing_id,
                        const std::string& callback_name_for_testing,
                        int extension_api_frame_id,
                        int extension_api_parent_frame_id) {
    results_.push_back(callback_name_for_testing);

    // If this fails, then the mapping is completely wrong.
    EXPECT_EQ(ToTestFrameId(render_process_id, frame_routing_id),
              extension_api_frame_id);
    EXPECT_EQ(ToTestParentFrameId(render_process_id, frame_routing_id),
              extension_api_parent_frame_id);
  }

  const std::vector<std::string>& results() { return results_; }
  void ClearResults() { results_.clear(); }

 private:
  content::TestBrowserThreadBundle thread_bundle_;
  // Used to verify the order of callbacks.
  std::vector<std::string> results_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionApiFrameIdMapTest);
};

}  // namespace

TEST_F(ExtensionApiFrameIdMapTest, GetFrameIdOnIO) {
  TestExtensionApiFrameIdMap map;
  EXPECT_EQ(0, map.GetInternalSize());

  // Two identical calls, should be processed at the next message loop.
  map.GetFrameIdOnIO(1, 2, CreateCallback(1, 2, "first"));
  EXPECT_EQ(1, map.GetInternalCallbackCount());
  EXPECT_EQ(0, map.GetInternalSize());

  map.GetFrameIdOnIO(1, 2, CreateCallback(1, 2, "first again"));
  EXPECT_EQ(2, map.GetInternalCallbackCount());
  EXPECT_EQ(0, map.GetInternalSize());

  // First get the frame ID on IO (queued on message loop), then set it on UI.
  // No callbacks should be invoked because the IO thread cannot know that the
  // frame ID was set on the UI thread.
  map.GetFrameIdOnIO(2, 1, CreateCallback(2, 1, "something else"));
  EXPECT_EQ(3, map.GetInternalCallbackCount());
  EXPECT_EQ(0, map.GetInternalSize());

  map.SetInternalFrameId(2, 1);
  EXPECT_EQ(1, map.GetInternalSize());
  EXPECT_EQ(0U, results().size());

  // Run some self-contained test. They should not affect the above callbacks.
  {
    // Callbacks for invalid IDs should immediately be run because it doesn't
    // require a thread hop to determine their invalidity.
    map.GetFrameIdOnIO(-1, MSG_ROUTING_NONE,
                       CreateCallback(-1, MSG_ROUTING_NONE, "invalid IDs"));
    EXPECT_EQ(3, map.GetInternalCallbackCount());  // No change.
    EXPECT_EQ(1, map.GetInternalSize());           // No change.
    ASSERT_EQ(1U, results().size());               // +1
    EXPECT_EQ("invalid IDs", results()[0]);
    ClearResults();
  }

  {
    // First set the frame ID on UI, then get it on IO. Callback should
    // immediately be invoked.
    map.SetInternalFrameId(3, 1);
    EXPECT_EQ(2, map.GetInternalSize());

    map.GetFrameIdOnIO(3, 1, CreateCallback(3, 1, "the only result"));
    EXPECT_EQ(3, map.GetInternalCallbackCount());  // No change.
    EXPECT_EQ(2, map.GetInternalSize());           // +1
    ASSERT_EQ(1U, results().size());               // +1
    EXPECT_EQ("the only result", results()[0]);
    ClearResults();
  }

  {
    // Request the frame ID on IO, set the frame ID (in reality, set on the UI),
    // and request another frame ID. The last query should cause both callbacks
    // to run because the frame ID is known at the time of the call.
    map.GetFrameIdOnIO(7, 2, CreateCallback(7, 2, "queued"));
    EXPECT_EQ(4, map.GetInternalCallbackCount());  // +1

    map.SetInternalFrameId(7, 2);
    EXPECT_EQ(3, map.GetInternalSize());           // +1

    map.GetFrameIdOnIO(7, 2, CreateCallback(7, 2, "not queued"));
    EXPECT_EQ(3, map.GetInternalCallbackCount());  // -1 (first callback ran).
    EXPECT_EQ(3, map.GetInternalSize());           // No change.
    ASSERT_EQ(2U, results().size());               // +2 (both callbacks ran).
    EXPECT_EQ("queued", results()[0]);
    EXPECT_EQ("not queued", results()[1]);
    ClearResults();
  }

  // A call identical to the very first call.
  map.GetFrameIdOnIO(1, 2, CreateCallback(1, 2, "same as first"));
  EXPECT_EQ(4, map.GetInternalCallbackCount());
  EXPECT_EQ(3, map.GetInternalSize());

  // Trigger the queued callbacks.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, map.GetInternalCallbackCount());   // -4 (no queued callbacks).

  EXPECT_EQ(4, map.GetInternalSize());            // +1 (1 new cached frame ID).
  ASSERT_EQ(4U, results().size());                // +4 (callbacks ran).

  // PostTasks are processed in order, so the very first callbacks should be
  // processed. As soon as the first callback is available, all of its callbacks
  // should be run (no deferrals!).
  EXPECT_EQ("first", results()[0]);
  EXPECT_EQ("first again", results()[1]);
  EXPECT_EQ("same as first", results()[2]);
  // This was queued after "first again", but has a different frame ID, so it
  // is received after "same as first".
  EXPECT_EQ("something else", results()[3]);
  ClearResults();

  // Request the frame ID for input that was already looked up. Should complete
  // synchronously.
  map.GetFrameIdOnIO(1, 2, CreateCallback(1, 2, "first and cached"));
  EXPECT_EQ(0, map.GetInternalCallbackCount());  // No change.
  EXPECT_EQ(4, map.GetInternalSize());           // No change.
  ASSERT_EQ(1U, results().size());               // +1 (synchronous callback).
  EXPECT_EQ("first and cached", results()[0]);
  ClearResults();

  // Trigger frame removal and look up frame ID. The frame ID should no longer
  // be available. and GetFrameIdOnIO() should require a thread hop.
  map.RemoveInternalFrameId(1, 2);
  EXPECT_EQ(3, map.GetInternalSize());           // -1
  map.GetFrameIdOnIO(1, 2, CreateCallback(1, 2, "first was removed"));
  EXPECT_EQ(1, map.GetInternalCallbackCount());  // +1
  ASSERT_EQ(0U, results().size());               // No change (queued callback).
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, map.GetInternalCallbackCount());  // -1 (callback not in queue).
  EXPECT_EQ(4, map.GetInternalSize());           // +1 (cached frame ID).
  ASSERT_EQ(1U, results().size());               // +1 (callback ran).
  EXPECT_EQ("first was removed", results()[0]);
}

}  // namespace extensions
