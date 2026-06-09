// Definitions for Registry<T>. Included at the end of registry.hxx; not standalone.

#include "autocog/utilities/exception.hxx"

namespace autocog::data {

template <class T>
std::string Registry<T>::add(std::unique_ptr<T> artifact) {
  // finalize() stamps the hash in construct mode, or re-verifies it in verify
  // mode (so adding an already-finalized artifact is safe and idempotent).
  artifact->finalize();
  if (!artifact->metadata || artifact->metadata->hash.empty())
    throw autocog::utilities::InternalError(
      "autocog::data::Registry: artifact has no hash after finalize");
  std::string const id = artifact->metadata->hash;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    entries_.emplace(id, Entry{std::move(artifact), 1});
  } else {
    // Identical content collapses onto the existing entry; the incoming
    // duplicate is freed when `artifact` leaves scope.
    ++it->second.holders;
  }
  return id;
}

template <class T>
T const & Registry<T>::get(std::string const & hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(hash);
  if (it == entries_.end())
    throw autocog::utilities::InternalError(
      "autocog::data::Registry: no artifact for hash " + hash);
  return *it->second.artifact;
}

template <class T>
void Registry<T>::release(std::string const & hash) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(hash);
  if (it == entries_.end()) return;  // unknown or already freed: no-op
  if (--it->second.holders <= 0)
    entries_.erase(it);
}

template <class T>
bool Registry<T>::contains(std::string const & hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.find(hash) != entries_.end();
}

}
