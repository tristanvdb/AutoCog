
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"

#include "autocog/codec/json.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <optional>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>

#include <unistd.h>
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"
#include <algorithm>

#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"

using namespace autocog::backend::llama;
namespace data  = autocog::data;
namespace codec = autocog::codec;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " --fta <file> (--model <file> | --rng) --ftt <file>\n"
              << "            [--seed N] [--ctx N]\n\n"
              << "Evaluate an FTA against a model and write the resulting FTT.\n\n"
              << "Options:\n"
              << "  --fta <file>          Input FTA JSON (required)\n"
              << "  --model <file>        Path to GGUF model file\n"
              << "  --rng                 Use built-in RNG model (no model file needed)\n"
              << "  --ftt <file>          Output FTT JSON (required; /dev/stdout for stdout)\n"
              << "  --seed N              RNG seed (default: 42)\n"
              << "  --ctx N               Maximum context size for the model\n"
              << "  --perf <file>         Write coarse-grain performance events\n"
              << "                        (ECS-flavored NDJSON; /dev/stdout for stdout)\n"
              << "  --verbose [LEVEL]     Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  --version             Show version\n"
              << "  --build-info          Show build configuration\n"
              << "  --help                Show this help message\n"
              << std::endl;
}

// --- Performance event emission (ECS-flavored NDJSON) -----------------------
// One JSON object per line, dotted-flat keys, mirroring the Python side's
// ECSFormatter (modules/autocog/_json_formatter.py) so a SIEM ingests both
// streams uniformly.

static std::string ecs_timestamp() {
  using namespace std::chrono;
  auto const now = system_clock::now();
  auto const t = system_clock::to_time_t(now);
  auto const ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
  return buf;
}

static std::string cpu_model_name() {
  std::ifstream f("/proc/cpuinfo");
  std::string line;
  while (std::getline(f, line)) {
    auto pos = line.find("model name");
    if (pos != std::string::npos) {
      auto colon = line.find(':');
      if (colon != std::string::npos) {
        auto v = line.substr(colon + 1);
        auto b = v.find_first_not_of(" \t");
        return b == std::string::npos ? v : v.substr(b);
      }
    }
  }
  return "unknown";
}

struct PerfEmitter {
  std::ofstream file;
  std::ostream * out = nullptr;
  nlohmann::json base;

  explicit PerfEmitter(std::string const & path) {
    if (path.empty()) return;
    if (path == "/dev/stdout") {
      out = &std::cout;
    } else {
      file.open(path);
      if (!file.is_open()) throw autocog::FileError("Failed to open: " + path, path);
      out = &file;
    }
    char host[256] = "unknown";
    gethostname(host, sizeof(host));
    base = {
      {"log.level", "info"},
      {"log.logger", "autocog.xfta.perf"},
      {"event.kind", "metric"},
      {"service.name", "autocog"},
      {"service.version", autocog::version()},
      {"host.name", host},
      {"autocog.perf.build_type", AUTOCOG_BUILD_TYPE},
      {"autocog.perf.host.cpu", cpu_model_name()},
      {"autocog.perf.host.hardware_concurrency", std::thread::hardware_concurrency()},
    };
  }

  void emit(std::string const & action, std::string const & message, nlohmann::json fields) {
    if (!out) return;
    nlohmann::json j = base;
    j["@timestamp"] = ecs_timestamp();
    j["event.action"] = action;
    j["message"] = message;
    for (auto const & [k, v] : fields.items()) j[k] = v;
    *out << j.dump() << "\n";
  }
};

static nlohmann::json kind_fields(std::string const & kind,
                                  PerfCounters::KindStats const & st) {
  std::string p = "autocog.perf." + kind + ".";
  return {
    {p + "calls", st.calls},
    {p + "seconds", st.seconds},
    {p + "tokens.restore", st.tokens_restore},
    {p + "tokens.eval", st.tokens_eval},
  };
}

static int run(int argc, char** argv) {
  std::string fta_file, ftt_file, model_path, perf_file;
  unsigned ctx_size = 4096;
  unsigned seed = 42;
  bool use_rng = false;

  autocog::init_console_logger();

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") { print_usage(argv[0]); return 0; }
    if (arg == "--version") { std::cout << "xfta " << autocog::version() << "\n"; return 0; }
    if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
    if (arg == "--rng") { use_rng = true; continue; }
    if (arg == "--fta"   && i + 1 < argc) { fta_file = argv[++i]; continue; }
    if (arg == "--ftt"   && i + 1 < argc) { ftt_file = argv[++i]; continue; }
    if (arg == "--model" && i + 1 < argc) { model_path = argv[++i]; continue; }
    if (arg == "--seed"  && i + 1 < argc) { seed = std::stoul(argv[++i]); continue; }
    if (arg == "--ctx"   && i + 1 < argc) { ctx_size = std::stoi(argv[++i]); continue; }
    if (arg == "--perf"  && i + 1 < argc) { perf_file = argv[++i]; continue; }
    if (arg == "--verbose") {
      spdlog::level::level_enum lvl = spdlog::level::debug;
      if (i + 1 < argc && autocog::looks_like_level_token(argv[i + 1])) {
        if (autocog::parse_level(argv[i + 1], lvl)) {
          ++i;
        } else {
          std::cerr << "Error: unknown verbosity level '" << argv[i + 1]
                    << "' (expected: trace, debug, info, warn, error, critical, off)\n";
          return 1;
        }
      }
      autocog::init_console_logger(lvl);
      continue;
    }
    std::cerr << "Error: Unknown option " << arg << "\n";
    print_usage(argv[0]);
    return 1;
  }

  if (fta_file.empty()) { std::cerr << "Error: --fta is required\n"; print_usage(argv[0]); return 1; }
  if (ftt_file.empty()) { std::cerr << "Error: --ftt is required\n"; print_usage(argv[0]); return 1; }
  if (model_path.empty() && !use_rng) { std::cerr << "Error: --model or --rng is required\n"; return 1; }

  Manager::initialize();
  PerfEmitter perf(perf_file);
  using pclock = std::chrono::steady_clock;

  auto const t_load = pclock::now();
  ModelID model_id;
  if (use_rng) {
    model_id = 0;
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Using built-in RNG model (Model #0)");
  } else {
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Loading model from {} with {} tokens of context", model_path, ctx_size);
    model_id = Manager::add_model(model_path, ctx_size);
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Model #{}", model_id);
  }
  double const load_seconds = std::chrono::duration<double>(pclock::now() - t_load).count();
  perf.emit("run.start", "xfta evaluation starting", {
    {"autocog.perf.model.path", use_rng ? "rng" : model_path},
    {"autocog.perf.model.n_ctx", ctx_size},
    {"autocog.perf.model.load_seconds", load_seconds},
    {"autocog.perf.seed", seed},
    {"autocog.perf.fta.path", fta_file},
  });

  Manager::get_model(model_id).set_seed(seed);
  SPDLOG_LOGGER_DEBUG(autocog::log(), "RNG seed: {}", seed);

  SPDLOG_LOGGER_DEBUG(autocog::log(), "FTA: \"{}\"", fta_file);
  auto fta = codec::from_file<data::FTA>(fta_file);
  EvalID eval_id = Manager::add_eval(model_id, *fta);
  Manager::advance(eval_id, std::nullopt);

  PerfCounters const & pc = Manager::get_eval(eval_id).perf();
  {
    // Bind each kind_fields() result to a named json before iterating:
    // items() returns a proxy referencing its json, and a temporary in a
    // range-for initializer is destroyed before the loop body runs (pre-C++23).
    nlohmann::json f = kind_fields("text", pc.text);
    nlohmann::json const fcomplete = kind_fields("complete", pc.complete);
    for (auto const & [k, v] : fcomplete.items()) f[k] = v;
    nlohmann::json const fchoose = kind_fields("choose", pc.choose);
    for (auto const & [k, v] : fchoose.items()) f[k] = v;
    f["autocog.perf.complete.tokens.lookahead"] = pc.lookahead_tokens;
    f["autocog.perf.prepare_seconds"] = pc.prepare_seconds;
    f["autocog.perf.advance_seconds"] = pc.advance_seconds;
    f["autocog.perf.tokens.restore"] =
        pc.text.tokens_restore + pc.complete.tokens_restore + pc.choose.tokens_restore;
    f["autocog.perf.tokens.eval"] =
        pc.text.tokens_eval + pc.complete.tokens_eval + pc.choose.tokens_eval;
    KvStats const & kv = Manager::get_model(model_id).kv_stats();
    f["autocog.perf.kv.slots"] = Manager::get_model(model_id).kv_slots();
    f["autocog.perf.kv.exact"] = kv.exact;
    f["autocog.perf.kv.extends"] = kv.extends;
    f["autocog.perf.kv.trims"] = kv.trims;
    f["autocog.perf.kv.forks"] = kv.forks;
    f["autocog.perf.kv.evictions"] = kv.evictions;
    f["autocog.perf.kv.tokens.primed"] = kv.tokens_primed;
    SearchStats const st = Manager::get_eval(eval_id).search_stats();
    f["autocog.perf.search.terminals"] = st.terminals;
    f["autocog.perf.search.coverage.fta"] = st.coverage_fta;
    f["autocog.perf.search.coverage.sta"] = st.coverage_sta;
    f["autocog.perf.search.stopped"] = st.stopped;
    f["autocog.perf.search.abandoned"] = st.abandoned;
    perf.emit("eval.summary", "evaluation complete", std::move(f));
  }

  // The backend grows the tree with tokens during evaluation; fill each node's
  // text from its tokens in one post-generation pass (needs the model).
  data::FTT ftt = Manager::retrieve(eval_id);
  detokenize(model_id, ftt);

  std::ostream * out = &std::cout;
  std::ofstream outfile;
  if (ftt_file != "/dev/stdout") {
    outfile.open(ftt_file);
    if (!outfile.is_open()) throw autocog::FileError("Failed to open: " + ftt_file, ftt_file);
    out = &outfile;
  }
  *out << codec::to_json(ftt).dump(2) << std::endl;
  SPDLOG_LOGGER_DEBUG(autocog::log(), "FTT: \"{}\"", ftt_file);

  Manager::rm_eval(eval_id);
  return 0;
}

int main(int argc, char** argv) {
  return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
