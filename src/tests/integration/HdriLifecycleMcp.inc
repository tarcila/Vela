// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

SCENARIO(
    "Public HDRI owner and session teardown release held preparation without publication",
    "[integration][content][mcp][public-hdri-teardown]")
{
  const bool owner = GENERATE(false, true);
  const auto physical =
      std::make_shared<vsr::rendering::FrameBudget>(256u << 20);
  const auto initialBytes = physical->used();
  const auto ownerThread = std::this_thread::get_id();
  std::atomic<size_t> workerBytes{0};
  {
    image::ExchangeLimits limits;
    limits.reservePhysicalMemory = [&, physical](size_t bytes) {
      auto lease = physical->reserve(bytes);
      // Track this fixture's sub-4-KiB worker allocator, not RPC Job/status
      // controls which correctly survive as historical result ownership.
      if (!lease || bytes >= 4096 || std::this_thread::get_id() == ownerThread)
        return lease;
      workerBytes.fetch_add(bytes);
      return std::shared_ptr<const void>(physical.get(),
          [lease = std::move(lease), &workerBytes, bytes](
              const void *) { workerBytes.fetch_sub(bytes); });
    };
    HdriFixture f(limits);
    f.scene.defaultLayer();
    HdriWorkerCheckpoint checkpoint;
    f.creator->setWorkerObserver(checkpoint.observer());
    auto session = f.attach();
    const auto originalSession = session.id;
    writePublicHdri(f.tmp.path + "/safety.hdr");
    f.tool(session, "vsr_create_hdri", publicHdriArgs(f, 1));
    checkpoint.wait(f);
    REQUIRE(workerBytes.load() > 0);
    if (owner) {
      // No handler may retain a reference to the disappearing host service.
      f.server.reset();
      std::atomic<bool> stopped{false};
      std::thread stop([&] {
        f.creator.reset();
        stopped.store(true);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      const bool joinedEarly = stopped.load();
      const auto heldBytes = workerBytes.load();
      // Release/join before any fallible assertion, including on a failure.
      checkpoint.released->store(true);
      stop.join();
      CHECK_FALSE(joinedEarly);
      CHECK(heldBytes > 0);
      mcp::Config cfg;
      mcp::tools::addLookupTools(cfg.tools, *f.engine);
      REQUIRE(
          mcp::McpServer::start(cfg, *f.authority, *f.engine, f.dir, f.server)
              .ok());
    } else {
      f.rpc("vsr/detach", "{}", &session);
      f.drain();
      CHECK(workerBytes.load() > 0);
      checkpoint.released->store(true);
      session = f.attach();
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    Reply status;
    do {
      f.drain();
      status = f.status(session, originalSession, 1);
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (
        status.str("state") != "terminal" || !status.str("progress").empty());
    CHECK_FALSE(status.flag("committed"));
    CHECK(status.str("outcome") != "ok");
    CHECK(workerBytes.load() == 0);
    CHECK(f.scene.numberOfObjects(ANARI_LIGHT) == 0);
    CHECK(f.scene.numberOfObjects(ANARI_ARRAY) == 0);
    CHECK(f.imageActivity->usage().restorableBatches == 0);
  }
  CHECK(physical->used() == initialBytes);
}
