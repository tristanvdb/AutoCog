// Definitions for Base<CRT>. Included at the end of base.hxx; not standalone.

namespace autocog::data {

template <class CRT>
void Base<CRT>::finalize() {
  std::string const h = hash();
  if (metadata) {
    // verify
    if (metadata->hash != h)
      throw autocog::IntegrityError("autocog::data: hash mismatch on finalize", CRT::format, metadata->hash, h);
  } else {
    // construct
    metadata = Metadata::make<CRT>(h);
  }
}

template <class CRT>
std::string Base<CRT>::hash() const {
  // hash( hash(provenance) + content_hash() )
  std::string prov;
  for (auto const & [role, h] : provenance) prov += role + '=' + h + ';';
  return digest(digest(prov) + static_cast<CRT const *>(this)->content_hash());
}

}
