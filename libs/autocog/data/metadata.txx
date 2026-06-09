// Template definitions for Metadata. Included at the end of metadata.hxx; not standalone.

namespace autocog::data {

template <class CRT>
Metadata Metadata::make(std::string hash) {
  Metadata m;
  m.format  = CRT::format;
  m.version = schema_version;
  m.hash    = std::move(hash);
  m.timestamp = utc_timestamp();
  return m;
}

}
