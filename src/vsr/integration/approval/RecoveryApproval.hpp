// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/session/SessionAuthority.hpp"
// std
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace vsr::integration::approval {

/* Trusted host control only. No transport tool may call approve(). Canonical
 * contents are host-produced, not a filename, timestamp or client assertion.
 * One proposal per store/launch, five-minute monotonic lifetime, bounded fully
 * charged storage. A claim pins physical storage even after invalidation. */
struct Identity
{
  std::string launchId;
  std::string sessionId;
  std::uint64_t target{0};
  std::string kind;
  std::uint64_t originalRequest{0};

  bool operator==(const Identity &other) const;
};

struct Proposal
{
  Identity identity;
  std::uint64_t requiredBytes{0};
  std::uint64_t softBytes{0};
  std::uint64_t hardBytes{0};
  std::uint64_t approvedMaximum{0};
  std::size_t canonicalCharge{0};
  double expiresAt{0.};
  bool claimed{false};
  std::string targetDescription;
};

class ProposalStore
{
  struct State;

 public:
  using Reserve = std::function<std::shared_ptr<const void>(std::size_t)>;

  class Claim
  {
    struct Key
    {
    };

   public:
    Claim(Key, std::shared_ptr<State> state);
    Claim(const Claim &) = delete;
    Claim &operator=(const Claim &) = delete;
    ~Claim();
    bool valid(std::uint64_t requiredBytes) const;
    // At the irreversible boundary. Allocation-free, at most once.
    bool consume(std::uint64_t requiredBytes);
    bool matches(const std::string &launchId,
        const std::string &sessionId,
        std::uint64_t target,
        const std::string &kind) const;
    bool bindReservation(
        const void *owner, std::uint64_t id, std::uint64_t requiredBytes);

   private:
    friend class ProposalStore;
    std::shared_ptr<State> m_state;
  };

  explicit ProposalStore(session::MonotonicClock clock = session::steadyClock(),
      std::size_t canonicalByteCap = 64u << 20);
  std::optional<Proposal> snapshot();
  std::size_t canonicalByteCap() const;
  bool offer(const Identity &identity,
      std::string_view canonical,
      std::uint64_t requiredBytes,
      std::uint64_t softBytes,
      std::uint64_t hardBytes,
      std::weak_ptr<const void> registration,
      const Reserve &reserve,
      std::string_view arguments = {},
      std::string_view layout = {});
  // The UI must display exact requiredBytes and request that finite maximum.
  bool approve(const Identity &identity, std::uint64_t maximum);
  bool revoke(const Identity &identity);
  void invalidateSession(const std::string &sessionId);
  std::shared_ptr<Claim> claim(const Identity &identity,
      std::string_view canonical,
      std::uint64_t requiredBytes,
      std::string_view arguments = {},
      std::string_view layout = {});

 private:
  void expire();
  session::MonotonicClock m_clock;
  const std::size_t m_cap;
  std::mutex m_mutex;
  std::shared_ptr<State> m_pending;
};

// Inlined definitions ////////////////////////////////////////////////////////

struct ProposalStore::State
{
  // Declared first: release coverage only after the canonical owners die.
  std::shared_ptr<const void> physicalLease;
  session::MonotonicClock clock;
  std::weak_ptr<const void> registration;
  std::string canonical;
  std::string arguments;
  std::string layout;
  Proposal proposal;
  const void *reservationOwner{nullptr};
  std::uint64_t reservationId{0};
  mutable std::mutex mutex;
  bool revoked{false};
  bool consumed{false};
};

inline bool Identity::operator==(const Identity &other) const
{
  return launchId == other.launchId && sessionId == other.sessionId
      && target == other.target && kind == other.kind
      && originalRequest == other.originalRequest;
}

inline ProposalStore::ProposalStore(
    session::MonotonicClock clock, std::size_t canonicalByteCap)
    : m_clock(std::move(clock)), m_cap(canonicalByteCap)
{}

inline std::size_t ProposalStore::canonicalByteCap() const
{
  return m_cap;
}

inline void ProposalStore::expire()
{
  if (!m_pending)
    return;
  bool invalid = false;
  {
    std::lock_guard<std::mutex> lock(m_pending->mutex);
    invalid = m_pending->revoked || m_pending->consumed
        || m_pending->registration.expired()
        || m_clock() >= m_pending->proposal.expiresAt;
    if (invalid)
      m_pending->revoked = true;
  }
  if (invalid)
    m_pending.reset();
}

inline std::optional<Proposal> ProposalStore::snapshot()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  expire();
  if (!m_pending)
    return {};
  std::lock_guard<std::mutex> stateLock(m_pending->mutex);
  return m_pending->proposal;
}

inline bool ProposalStore::offer(const Identity &identity,
    std::string_view canonical,
    std::uint64_t requiredBytes,
    std::uint64_t softBytes,
    std::uint64_t hardBytes,
    std::weak_ptr<const void> registration,
    const Reserve &reserve,
    std::string_view arguments,
    std::string_view layout)
{
  constexpr std::size_t CONTROL_CHARGE = 4096;
  if (!m_clock || !reserve || identity.launchId.empty()
      || identity.sessionId.empty() || identity.kind.empty()
      || !identity.originalRequest || !softBytes || hardBytes < softBytes
      || requiredBytes <= hardBytes || registration.expired()
      || identity.launchId.size() > 256 || identity.sessionId.size() > 256
      || identity.kind.size() > 64 || m_cap < CONTROL_CHARGE
      || canonical.size() > m_cap - CONTROL_CHARGE
      || arguments.size() > m_cap - CONTROL_CHARGE - canonical.size()
      || layout.size()
          > m_cap - CONTROL_CHARGE - canonical.size() - arguments.size())
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  expire();
  // Never replace a live proposal or renew its original expiration.
  if (m_pending)
    return false;
  const auto bytes =
      canonical.size() + arguments.size() + layout.size() + CONTROL_CHARGE;
  auto lease = reserve(bytes);
  if (!lease)
    return false;
  auto state = std::make_shared<State>();
  state->physicalLease = std::move(lease);
  state->clock = m_clock;
  state->registration = std::move(registration);
  state->canonical.assign(canonical.data(), canonical.size());
  state->arguments.assign(arguments.data(), arguments.size());
  state->layout.assign(layout.data(), layout.size());
  state->proposal = {identity,
      requiredBytes,
      softBytes,
      hardBytes,
      0,
      bytes,
      m_clock() + 300.,
      false,
      std::string(arguments.substr(0, 1024))};
  m_pending = std::move(state);
  return true;
}

inline bool ProposalStore::approve(
    const Identity &identity, std::uint64_t maximum)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  expire();
  if (!m_pending)
    return false;
  std::lock_guard<std::mutex> stateLock(m_pending->mutex);
  auto &proposal = m_pending->proposal;
  if (!(proposal.identity == identity) || proposal.claimed
      || maximum != proposal.requiredBytes)
    return false;
  proposal.approvedMaximum = maximum;
  return true;
}

inline bool ProposalStore::revoke(const Identity &identity)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  expire();
  if (!m_pending)
    return false;
  {
    std::lock_guard<std::mutex> stateLock(m_pending->mutex);
    if (!(m_pending->proposal.identity == identity))
      return false;
    m_pending->revoked = true;
  }
  m_pending.reset();
  return true;
}

inline void ProposalStore::invalidateSession(const std::string &sessionId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_pending)
    return;
  bool matching = false;
  {
    std::lock_guard<std::mutex> stateLock(m_pending->mutex);
    matching = m_pending->proposal.identity.sessionId == sessionId;
    if (matching)
      m_pending->revoked = true;
  }
  if (matching)
    m_pending.reset();
}

inline std::shared_ptr<ProposalStore::Claim> ProposalStore::claim(
    const Identity &identity,
    std::string_view canonical,
    std::uint64_t requiredBytes,
    std::string_view arguments,
    std::string_view layout)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  expire();
  if (!m_pending)
    return {};
  std::lock_guard<std::mutex> stateLock(m_pending->mutex);
  auto &proposal = m_pending->proposal;
  if (!(proposal.identity == identity) || proposal.claimed
      || !proposal.approvedMaximum || requiredBytes != proposal.requiredBytes
      || canonical != m_pending->canonical || arguments != m_pending->arguments
      || layout != m_pending->layout)
    return {};
  // Allocate the control before marking exclusive ownership. The private
  // constructor prevents callers from forging a permit from a Boolean.
  auto result = std::make_shared<Claim>(Claim::Key{}, m_pending);
  m_pending->reservationOwner = nullptr;
  m_pending->reservationId = 0;
  proposal.claimed = true;
  return result;
}

inline ProposalStore::Claim::Claim(Key, std::shared_ptr<State> state)
    : m_state(std::move(state))
{}

inline ProposalStore::Claim::~Claim()
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (!m_state->consumed)
    m_state->proposal.claimed = false;
}

inline bool ProposalStore::Claim::valid(std::uint64_t requiredBytes) const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return !m_state->revoked && !m_state->consumed
      && !m_state->registration.expired()
      && m_state->clock() < m_state->proposal.expiresAt
      && m_state->proposal.claimed
      && requiredBytes == m_state->proposal.requiredBytes
      && requiredBytes == m_state->proposal.approvedMaximum;
}

inline bool ProposalStore::Claim::consume(std::uint64_t requiredBytes)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (m_state->revoked || m_state->consumed || m_state->registration.expired()
      || m_state->clock() >= m_state->proposal.expiresAt
      || !m_state->proposal.claimed
      || requiredBytes != m_state->proposal.requiredBytes
      || requiredBytes != m_state->proposal.approvedMaximum)
    return false;
  m_state->consumed = true;
  return true;
}

inline bool ProposalStore::Claim::matches(const std::string &launchId,
    const std::string &sessionId,
    std::uint64_t target,
    const std::string &kind) const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  const auto &identity = m_state->proposal.identity;
  return identity.launchId == launchId && identity.sessionId == sessionId
      && identity.target == target && identity.kind == kind;
}

inline bool ProposalStore::Claim::bindReservation(
    const void *owner, std::uint64_t id, std::uint64_t requiredBytes)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (!owner || !id || m_state->reservationOwner || m_state->revoked
      || m_state->consumed || m_state->registration.expired()
      || m_state->clock() >= m_state->proposal.expiresAt
      || requiredBytes != m_state->proposal.requiredBytes
      || requiredBytes != m_state->proposal.approvedMaximum)
    return false;
  m_state->reservationOwner = owner;
  m_state->reservationId = id;
  return true;
}

} // namespace vsr::integration::approval
