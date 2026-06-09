#include "autocog/data/channel.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void put_paths(ContentHasher & h, std::vector<PathStep> const & v) {
  h.put(static_cast<unsigned>(v.size()));
  for (auto const & p : v) h.put(p.hash());
}
void put_clauses(ContentHasher & h, std::vector<Clause> const & v) {
  h.put(static_cast<unsigned>(v.size()));
  for (auto const & c : v) h.put(c.hash());
}

}  // namespace

std::string Clause::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(value.index()));
  std::visit(overloaded{
    [&](BindClause const & c)   { put_paths(h, c.source); put_paths(h, c.target); },
    [&](RavelClause const & c)  { h.put(c.depth); put_paths(h, c.target); },
    [&](WrapClause const & c)   { put_paths(h, c.target); },
    [&](PruneClause const & c)  { put_paths(h, c.target); },
    [&](MappedClause const & c) { put_paths(h, c.target); },
  }, value);
  return h.hash();
}

std::string ChannelKwarg::hash() const {
  ContentHasher h;
  h.put(name).put(is_input);
  put_paths(h, path);
  put_clauses(h, clauses);
  h.put(prompt).put(value);
  return h.hash();
}

std::string Channel::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(value.index()));
  std::visit(overloaded{
    [&](InputChannel const & c) {
      put_paths(h, c.target); put_paths(h, c.source);
    },
    [&](DataflowChannel const & c) {
      put_paths(h, c.target); put_paths(h, c.source); put_clauses(h, c.clauses); h.put(c.prompt);
    },
    [&](CallChannel const & c) {
      put_paths(h, c.target);
      h.put(static_cast<unsigned>(c.kwargs.size()));
      for (auto const & kw : c.kwargs) h.put(kw.hash());
      put_clauses(h, c.clauses);
      h.put(c.extern_func).put(c.entry);
    },
  }, value);
  return h.hash();
}

}
