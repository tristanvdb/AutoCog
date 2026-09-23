
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/logging.hxx"

#include <set>
#include <string>
#include <vector>

namespace autocog::compiler::stl {

// ============================================================================
// Stage 5.5 — `check`: semantic validation over the assembled IR.
//
// The first point where records are inlined and field trees are concrete (so
// paths resolve) while the whole program is still one IR (so cross-prompt
// references are visible). Checks:
//   C1  field-path resolution: return sources, dataflow channel targets and
//       `use` sources (local and cross-prompt), call-channel targets and
//       non-input kwarg paths, select/repeat choice sources.
//   use/get confusion: a `get` (input) source whose name matches a local
//       field is almost certainly a `use` written as `get` — warning.
// `get` sources name calling-frame values and are open-world by design; they
// are never errors here.
// ============================================================================

namespace {

// Resolve a name path against a field tree (names only: selectors/bounds are
// C3's concern). Returns the terminal field, or nullptr.
ir::Field const * resolve_steps(
    std::vector<ir::PathStep> const & steps,
    std::vector<std::unique_ptr<ir::Field>> const & fields
) {
    auto const * level = &fields;
    ir::Field const * hit = nullptr;
    for (auto const & step : steps) {
        hit = nullptr;
        if (!level) return nullptr;  // walked past a leaf
        for (auto const & f : *level) {
            if (f && f->name == step.name) { hit = f.get(); break; }
        }
        if (!hit) return nullptr;
        level = std::get_if<std::vector<std::unique_ptr<ir::Field>>>(&hit->format);
    }
    return hit;
}

std::string path_text(std::vector<ir::PathStep> const & steps) {
    std::string out;
    for (auto const & s : steps) {
        if (!out.empty()) out += ".";
        out += s.name;
    }
    return out;
}

// --- C3 shape helpers -------------------------------------------------------
// Shape checks are conservative: clauses (ravel/bind/wrap/prune) transform
// shapes in ways this stage does not model, so any clause suppresses the
// check. Only statically provable mismatches are flagged.

bool is_struct_field(ir::Field const & f) {
    return std::holds_alternative<std::vector<std::unique_ptr<ir::Field>>>(f.format);
}

// Minimum element count the field requires (1 for non-arrays).
int min_count(ir::Field const & f) {
    return f.range ? f.range->first : 1;
}
// Maximum element count the field can hold (1 for non-arrays).
int max_count(ir::Field const & f) {
    return f.range ? f.range->second : 1;
}

// The callee-side input signature: names pulled from the calling frame by
// `get` channels and by `get`-sourced call arguments.
std::set<std::string> input_signature(ir::Prompt const & pmt) {
    std::set<std::string> names;
    for (auto const & ch : pmt.channels) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ir::InputChannel>) {
                if (!c.source.empty()) names.insert(c.source.front().name);
            } else if constexpr (std::is_same_v<T, ir::CallChannel>) {
                for (auto const & [kw, arg] : c.kwargs) {
                    (void)kw;
                    if (arg.is_input && !arg.path.empty())
                        names.insert(arg.path.front().name);
                }
            }
        }, ch);
    }
    return names;
}

bool has_top_field(ir::Prompt const & pmt, std::string const & name) {
    for (auto const & f : pmt.fields)
        if (f && f->name == name) return true;
    return false;
}

// Walk every field of a prompt, calling fn(field, path-so-far).
template <typename Fn>
void walk_fields(std::vector<std::unique_ptr<ir::Field>> const & fields,
                 std::string const & prefix, Fn && fn) {
    for (auto const & f : fields) {
        if (!f) continue;
        std::string here = prefix.empty() ? f->name : prefix + "." + f->name;
        fn(*f, here);
        if (auto const * sub =
                std::get_if<std::vector<std::unique_ptr<ir::Field>>>(&f->format))
            walk_fields(*sub, here, fn);
    }
}

}  // namespace

std::optional<int> Driver::run_check() {
    // Prompt lookup by declared or mangled name (cross-prompt references may
    // carry either depending on specialization).
    auto find_prompt = [&](std::string const & name) -> ir::Prompt const * {
        auto it = prompts.find(name);
        if (it != prompts.end()) return it->second.get();
        for (auto const & [mangled, p] : prompts) {
            if (p && (p->name == name || p->mangled_name == name)) return p.get();
        }
        return nullptr;
    };

    for (auto const & [mangled, pmt_ptr] : prompts) {
        if (!pmt_ptr) continue;
        auto const & pmt = *pmt_ptr;
        std::string const where = " (prompt '" + pmt.name + "')";

        // Resolve a path in this prompt or, when `prompt` names another one,
        // against that prompt's fields. Emits the error; returns success.
        auto check_path = [&](std::vector<ir::PathStep> const & steps,
                              std::optional<std::string> const & other,
                              std::string const & what) -> bool {
            if (steps.empty()) return true;
            ir::Prompt const * scope_pmt = &pmt;
            if (other) {
                scope_pmt = find_prompt(*other);
                if (!scope_pmt) {
                    emit_error(what + " references unknown prompt '" + *other
                               + "'" + where, std::nullopt);
                    return false;
                }
            }
            if (!resolve_steps(steps, scope_pmt->fields)) {
                emit_error(what + " '" + (other ? *other + "." : std::string{})
                           + path_text(steps) + "' does not name a field of prompt '"
                           + scope_pmt->name + "'" + where, std::nullopt);
                return false;
            }
            return true;
        };

        // C1: return sources.
        if (pmt.return_info) {
            for (auto const & rf : pmt.return_info->fields) {
                if (rf.constant || rf.source.is_input) continue;
                check_path(rf.source.steps, rf.source.prompt, "return path");
            }
        }

        // C1: channels.
        for (auto const & ch : pmt.channels) {
            std::visit([&](auto const & c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, ir::InputChannel>) {
                    check_path(c.target.steps, std::nullopt, "channel target");
                    // `get` pulls from the calling frame — open-world, never an
                    // error. But a source that names a local field OTHER than
                    // the channel's own target is almost certainly a `use`
                    // written as `get`. (`x get x` — fill field x from input
                    // x — is the standard idiom and stays silent.)
                    if (c.source.size() == 1 && !c.target.steps.empty()
                            && c.source.front().name != c.target.steps.front().name
                            && has_top_field(pmt, c.source.front().name)) {
                        emit_warning(
                            "channel source '" + c.source.front().name
                            + "' is an input (get) but a local field of the same "
                            "name exists; did you mean `use "
                            + c.source.front().name + "`?" + where,
                            std::nullopt);
                    }
                } else if constexpr (std::is_same_v<T, ir::DataflowChannel>) {
                    bool const ok_t = check_path(c.target.steps, std::nullopt,
                                                 "channel target");
                    bool const ok_s = check_path(c.source, c.prompt,
                                                 "channel `use` source");
                    // C3: shape compatibility, only when both ends resolved
                    // and no clause reshapes the flow.
                    if (ok_t && ok_s && c.clauses.empty()
                            && !c.target.steps.empty() && !c.source.empty()) {
                        auto const * tgt = resolve_steps(c.target.steps, pmt.fields);
                        ir::Prompt const * src_pmt = c.prompt ? find_prompt(*c.prompt) : &pmt;
                        auto const * src = src_pmt
                            ? resolve_steps(c.source, src_pmt->fields) : nullptr;
                        if (tgt && src) {
                            if (is_struct_field(*tgt) != is_struct_field(*src)) {
                                emit_error(
                                    "channel '" + path_text(c.target.steps)
                                    + "' connects a struct field and a leaf field"
                                    + where, std::nullopt);
                            } else if (min_count(*tgt) > max_count(*src)) {
                                emit_error(
                                    "channel '" + path_text(c.target.steps)
                                    + "' requires at least "
                                    + std::to_string(min_count(*tgt))
                                    + " element(s) but its source '"
                                    + path_text(c.source) + "' provides at most "
                                    + std::to_string(max_count(*src)) + where,
                                    std::nullopt);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, ir::CallChannel>) {
                    check_path(c.target.steps, std::nullopt, "channel target");
                    for (auto const & [kw, arg] : c.kwargs) {
                        if (arg.value || arg.is_input) continue;
                        check_path(arg.path, arg.prompt,
                                   "call argument '" + kw + "' source");
                    }
                    // C3: call-site coverage against a callee PROMPT's input
                    // signature (extern python calls have no checkable
                    // signature). Unknown kwarg = error; missing input =
                    // warning (a missing input leaves the target field to
                    // free-generate, which may be intended).
                    if (c.entry) {
                        if (auto const * callee = find_prompt(*c.entry)) {
                            auto const sig = input_signature(*callee);
                            for (auto const & [kw, arg] : c.kwargs) {
                                (void)arg;
                                if (!sig.count(kw)) {
                                    emit_error(
                                        "call argument '" + kw + "' is not an "
                                        "input of prompt '" + callee->name + "' "
                                        "(inputs: " + [&]{
                                            std::string s;
                                            for (auto const & n : sig)
                                                s += (s.empty() ? "" : ", ") + n;
                                            return s.empty() ? std::string("none") : s;
                                        }() + ")" + where, std::nullopt);
                                }
                            }
                            for (auto const & name : sig) {
                                if (!c.kwargs.count(name)) {
                                    emit_warning(
                                        "call to prompt '" + callee->name
                                        + "' does not bind its input '" + name
                                        + "'; the fed field will free-generate"
                                        + where, std::nullopt);
                                }
                            }
                        }
                    }
                }
            }, ch);
        }

        // C1: select/repeat choice sources (a bad path becomes an empty
        // choice list and dies deep in the backend as an internal error).
        walk_fields(pmt.fields, "", [&](ir::Field const & f, std::string const & fpath) {
            if (auto const * choice = std::get_if<ir::Choice>(&f.format)) {
                if (!resolve_steps(choice->path, pmt.fields)) {
                    emit_error(
                        choice->mode + " source '" + path_text(choice->path)
                        + "' (field '" + fpath + "') does not name a field of "
                        "prompt '" + pmt.name + "'", std::nullopt);
                }
            }
        });
    }

    if (report_errors()) return 6;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "IR checked (#5.5): {} prompts", prompts.size());
    return std::nullopt;
}

}  // namespace autocog::compiler::stl
