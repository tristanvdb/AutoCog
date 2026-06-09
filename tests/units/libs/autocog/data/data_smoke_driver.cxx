// Smoke tests for autocog::data: build or load each artifact, finalize, and
// round-trip it through JSON (and one through Python) to confirm the module
// runs end to end. Returns non-zero if any check fails.
//
// Usage: data_smoke_driver <share_dir>   (defaults to "share")

#include "autocog/codec/json.hxx"
#include "autocog/codec/python.hxx"
using namespace autocog::codec;

#include "autocog/utilities/errors.hxx"

#include <nlohmann/json.hpp>
#include <pybind11/embed.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using namespace autocog::data;

namespace {

int failures = 0;

void check(bool ok, std::string const & what) {
  if (ok) std::cout << "ok   : " << what << "\n";
  else  { std::cerr << "FAIL : " << what << "\n"; ++failures; }
}

std::string read_file(std::string const & path) {
  std::ifstream in(path);
  if (!in) { std::cerr << "FAIL : cannot open " << path << "\n"; ++failures; return "{}"; }
  std::stringstream ss; ss << in.rdbuf();
  return ss.str();
}

// Serialize a finalized artifact, reload it (verify-mode), and confirm the
// metadata survives and re-serialization is stable.
template <class T>
void json_roundtrip(T const & obj, std::string const & name) {
  std::string text = to_string(obj);
  auto reloaded = from_string<T>(text);   // from_string -> finalize (verify)
  check(reloaded != nullptr, name + " json reload");
  if (!reloaded) return;
  check(reloaded->metadata.has_value(), name + " metadata present after reload");
  check(obj.metadata.has_value() && reloaded->metadata->hash == obj.metadata->hash,
        name + " hash stable across round-trip");
  check(to_string(*reloaded) == text, name + " json idempotent");

  // file round-trip (to_file -> from_file)
  std::string path = "/tmp/autocog_smoke_" + name + ".json";
  to_file(obj, path);
  auto fromfile = from_file<T>(path);
  check(fromfile != nullptr, name + " file reload");
  check(fromfile && fromfile->metadata.has_value()
        && obj.metadata.has_value() && fromfile->metadata->hash == obj.metadata->hash,
        name + " file round-trip hash stable");
  std::remove(path.c_str());
}

}  // namespace

int main(int argc, char ** argv) {
  std::string share = (argc > 1) ? argv[1] : "share";

  // --- FTT: construct a small tree -----------------------------------------
  {
    FTT ftt;
    ftt.root.action = 0;
    ftt.root.text = "root";
    FTTNode child;
    child.action = 1;
    child.text = "child";
    child.logprob = -0.5f;
    child.length = 1;
    child.field = 2;
    child.tokens = {7, 8};
    ftt.root.children.push_back(child);
    ftt.finalize();
    check(ftt.metadata && ftt.metadata->format == std::string("ftt"), "ftt format stamped");
    json_roundtrip(ftt, "ftt");
  }

  // --- FTA: one of each action kind + a vocab ------------------------------
  {
    FTA fta;
    fta.queue_metric = "logprob";
    { Action a; a.uid = "a0"; TextAction t; t.text = "hello"; a.body = t; a.successors = {"a1"}; fta.actions.push_back(a); }
    { Action a; a.uid = "a1"; ChooseAction c; c.choices = {"x", "y"}; c.threshold = 0.5f; c.width = 2; a.body = c; a.successors = {"a2", "a2"}; a.field = 0; fta.actions.push_back(a); }
    { Action a; a.uid = "a2"; CompleteAction c; c.length = 8; c.threshold = 0.1f; c.beams = 1; c.ahead = 1; c.width = 1; c.stop_text = "\n"; c.vocab = "vocab_x"; a.body = c; fta.actions.push_back(a); }
    VocabExpr leaf; leaf.kind = VocabExpr::Kind::Tokenize; leaf.strings = {"0", "1"};
    fta.vocabs.emplace("vocab_x", leaf);
    fta.finalize();
    check(fta.metadata && fta.metadata->format == std::string("fta"), "fta format stamped");
    json_roundtrip(fta, "fta");
  }

  // --- STA: one prompt, one entry point, one import ------------------------
  {
    STA sta;
    EntryPoint ep; ep.prompt = "main";
    SchemaField sf; sf.type = "text"; sf.required = true; sf.max_length = 64;
    ep.inputs.emplace("question", sf);
    sta.entry_points.emplace("main", ep);
    PythonImport imp; imp.file = "m.py"; imp.target = "fn";
    sta.python_imports.emplace("m", imp);
    Prompt p; p.name = "main"; p.desc = {"a prompt"};
    Field f; f.name = "answer"; f.depth = 1; f.index = 0; f.flat_index = 0; f.desc = {"the answer"};
    CompletionFormat cf; cf.length = 16; f.format.value = cf;
    p.fields.push_back(f);
    p.abstracts.push_back(Abstract{0, -1, -1});
    FlowControl fc; fc.prompt = "next"; Flow fl; fl.value = fc; p.flows.emplace("step", fl);
    InputChannel ic; ic.target = {PathStep{"answer", std::nullopt}}; ic.source = {PathStep{"question", std::nullopt}};
    Channel ch; ch.value = ic; p.channels.push_back(ch);
    sta.prompts.emplace("main", p);
    sta.finalize();
    check(sta.metadata && sta.metadata->format == std::string("sta"), "sta format stamped");
    json_roundtrip(sta, "sta");
  }

  // --- Syntax: load the real config input ----------------------------------
  {
    auto syn = from_string<Syntax>(read_file(share + "/syntax/default.json"));
    check(syn != nullptr, "syntax load from share/syntax/default.json");
    if (syn) {
      check(syn->metadata && syn->metadata->format == std::string("syntax"), "syntax stamped on load");
      json_roundtrip(*syn, "syntax");
    }
  }

  // --- SearchConfig: load the real config input ----------------------------
  {
    auto sc = from_string<SearchConfig>(read_file(share + "/search/default.json"));
    check(sc != nullptr, "search load from share/search/default.json");
    if (sc) {
      check(sc->metadata && sc->metadata->format == std::string("search"), "search stamped on load");
      json_roundtrip(*sc, "search");
    }
  }

  // --- Python codec smoke (under an embedded interpreter) ------------------
  {
    pybind11::scoped_interpreter guard{};
    FTT ftt; ftt.root.action = 0; ftt.root.text = "r"; ftt.finalize();
    auto back = from_py<FTT>(to_py(ftt));   // to_py -> from_py (verify)
    check(back != nullptr && back->metadata && back->metadata->hash == ftt.metadata->hash,
          "ftt python round-trip");
  }

  // --- typed-exception throw paths ----------------------------------------
  {
    // FileError: opening a nonexistent file for reading.
    bool threw = false;
    try { from_file<FTA>("/no/such/dir/missing.json"); }
    catch (autocog::FileError const & e) {
      threw = true;
      check(e.path == "/no/such/dir/missing.json", "FileError carries the path");
    } catch (...) {}
    check(threw, "from_file on a missing path throws FileError");
  }
  {
    // IntegrityError: reload an artifact whose stored hash has been tampered.
    FTT ftt; ftt.root.action = 0; ftt.root.text = "r"; ftt.finalize();
    nlohmann::json j = nlohmann::json::parse(to_string(ftt));
    j["metadata"]["hash"] = "deadbeef";
    bool threw = false;
    try { from_string<FTT>(j.dump()); }
    catch (autocog::IntegrityError const & e) {
      threw = true;
      check(e.format == std::string("ftt"), "IntegrityError carries the format");
      check(e.expected == "deadbeef", "IntegrityError.expected is the stored hash");
      check(!e.actual.empty() && e.actual != e.expected,
            "IntegrityError.actual is the recomputed hash");
    } catch (...) {}
    check(threw, "tampered stored hash on reload throws IntegrityError");
  }
  {
    // SchemaError: an unknown discriminant tag during deserialization.
    bool threw = false;
    try { VocabExpr ve; from_json(nlohmann::json{{"kind", "bogus"}}, ve); }
    catch (autocog::SchemaError const & e) {
      threw = true;
      check(e.path == "bogus", "SchemaError carries the offending tag as path");
    } catch (...) {}
    check(threw, "unknown discriminant tag throws SchemaError");
  }

  if (failures) { std::cerr << "\n" << failures << " smoke check(s) failed\n"; return 1; }
  std::cout << "\nall smoke checks passed\n";
  return 0;
}
