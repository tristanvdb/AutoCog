#ifndef AUTOCOG_DATA_REGISTRY_HXX
#define AUTOCOG_DATA_REGISTRY_HXX

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace autocog::data {

/// A reference-counted, content-addressed store of artifacts of one format @p T.
///
/// Entries are keyed by the artifact's own content hash (Metadata::hash) and
/// owned by the registry. Objects are immutable once added -- only a const
/// accessor exists -- which is what makes content-dedup and shared references
/// safe. Every add records one holder; identical content collapses onto a
/// single entry and just bumps the count; release drops a holder and frees the
/// entry only when the last one lets go. Thread-safe: the bindings drive it
/// from multiple threads.
template <class T>
class Registry {
  public:
    /// Finalize @p artifact (stamping its hash), take ownership, and store it
    /// under that hash, recording one holder. Identical content returns the
    /// existing hash and bumps its count. Returns the hash. Throws
    /// InternalError if the artifact has no hash after finalize.
    std::string add(std::unique_ptr<T> artifact);

    /// Read-only access to the entry at @p hash. Throws InternalError if @p
    /// hash is absent. There is deliberately no mutable accessor.
    T const & get(std::string const & hash) const;

    /// Drop one holder of @p hash; frees the entry when the last holder lets
    /// go. Releasing an unknown or already-freed hash is a no-op.
    void release(std::string const & hash);

    bool contains(std::string const & hash) const;

  private:
    struct Entry {
        std::unique_ptr<T> artifact;
        int                holders = 0;
    };
    std::map<std::string, Entry> entries_;  ///< hash -> owned artifact + holders
    mutable std::mutex           mutex_;
};

}

#include "autocog/data/registry.txx"

#endif // AUTOCOG_DATA_REGISTRY_HXX
