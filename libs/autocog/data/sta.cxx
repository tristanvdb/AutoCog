#include "autocog/data/sta.hxx"

#include "autocog/data/utility.hxx"

#include <cstdint>

namespace autocog::data {

namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void put_paths(ContentHasher & h, std::vector<PathStep> const & v) {
  h.put(static_cast<unsigned>(v.size()));
  for (auto const & p : v) h.put(p.hash());
}

void put_sv(ContentHasher & h, autocog::types::Value const & v) {
  h.put(static_cast<unsigned>(v.index()));
  std::visit(overloaded{
    [&](int x){ h.put(static_cast<std::int32_t>(x)); },
    [&](float x){ h.put(x); },
    [&](bool x){ h.put(x); },
    [&](std::string const & x){ h.put(x); },
    [&](std::monostate){ },
  }, v);
}

}  // namespace

std::string SearchParams::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(categories.size()));
  for (auto const & [cat, params] : categories) {
    h.put(cat);
    h.put(static_cast<unsigned>(params.size()));
    for (auto const & [key, val] : params) { h.put(key); put_sv(h, val); }
  }
  return h.hash();
}

std::string FieldFormat::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(value.index()));
  std::visit(overloaded{
    [&](std::monostate){ },
    [&](CompletionFormat const & f){ h.put(f.length); h.put(f.vocab); },
    [&](EnumFormat const & f){ h.put(f.values); },
    [&](ChoiceFormat const & f){ h.put(f.mode); put_paths(h, f.path); },
  }, value);
  return h.hash();
}

std::string Flow::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(value.index()));
  std::visit(overloaded{
    [&](FlowControl const & f){ h.put(f.prompt); h.put(f.limit); },
    [&](FlowReturn const & f){
      h.put(static_cast<unsigned>(f.fields.size()));
      for (auto const & rf : f.fields) { h.put(rf.alias); put_paths(h, rf.path); }
    },
  }, value);
  return h.hash();
}

std::string Field::hash() const {
  ContentHasher h;
  h.put(name).put(static_cast<std::int32_t>(depth)).put(static_cast<std::int32_t>(index))
   .put(static_cast<std::int32_t>(flat_index)).put(desc);
  h.put(range.has_value());
  if (range) { h.put(static_cast<std::int32_t>(range->first)).put(static_cast<std::int32_t>(range->second)); }
  h.put(format.hash());
  h.put(format_ref);
  h.put(format_desc);
  h.put(search.hash());
  return h.hash();
}

std::string SchemaField::hash() const {
  // Hash exactly the fields the codec serializes for this type. SchemaField is
  // a flat struct shared across types, so a member relevant to one type (e.g. a
  // text field's max_length/enum_values) may be left set when the field is an
  // array. The codec only serializes the type-relevant members, so the hash
  // must too -- otherwise two fields with identical serialized form would hash
  // differently and the artifact would not round-trip through finalize.
  ContentHasher h;
  h.put(type).put(required);
  if (type == "array") {
    h.put(items_type.empty() ? std::string("text") : items_type)
     .put(items_max_length).put(length).put(min_items).put(max_items);
  } else {
    h.put(max_length).put(enum_values);
  }
  return h.hash();
}

std::string EntryPoint::hash() const {
  ContentHasher h;
  h.put(prompt);
  h.put(static_cast<unsigned>(inputs.size()));
  for (auto const & [k, sf] : inputs)  h.put(k).put(sf.hash());
  h.put(static_cast<unsigned>(outputs.size()));
  for (auto const & [k, sf] : outputs) h.put(k).put(sf.hash());
  return h.hash();
}

std::string PythonImport::hash() const { ContentHasher h; h.put(file).put(target); return h.hash(); }

std::string Prompt::hash() const {
  ContentHasher h;
  h.put(name).put(desc);
  h.put(static_cast<unsigned>(fields.size()));
  for (auto const & f : fields) h.put(f.hash());
  h.put(static_cast<unsigned>(abstracts.size()));
  for (auto const & a : abstracts)
    h.put(static_cast<std::int32_t>(a.field)).put(static_cast<std::int32_t>(a.flow)).put(static_cast<std::int32_t>(a.exit_));
  h.put(static_cast<unsigned>(flows.size()));
  for (auto const & [k, flow] : flows) h.put(k).put(flow.hash());
  h.put(static_cast<unsigned>(channels.size()));
  for (auto const & c : channels) h.put(c.hash());
  h.put(search.hash());
  h.put(static_cast<unsigned>(vocabs.size()));
  for (auto const & [k, ve] : vocabs) h.put(k).put(ve.hash());
  return h.hash();
}

std::string STA::content_hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(entry_points.size()));
  for (auto const & [k, e] : entry_points) h.put(k).put(e.hash());
  h.put(static_cast<unsigned>(python_imports.size()));
  for (auto const & [k, imp] : python_imports) h.put(k).put(imp.hash());
  h.put(static_cast<unsigned>(prompts.size()));
  for (auto const & [k, p] : prompts) h.put(k).put(p.hash());
  return h.hash();
}

}
