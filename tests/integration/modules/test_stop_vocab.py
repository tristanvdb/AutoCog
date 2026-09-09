"""Stop-as-vocab semantics through the Python surface.

The mask-level guarantees: a completion with `stop=""` fills its exact token
budget through its vocab, and the stop set is unioned into the generation
mask so restrictive vocabs still terminate. The real-model variants exercise
the full Llama 3 tokenizer (glued and multi-byte tokens included).
"""

EXACT_PIN = '''
vocab digit = tokenize("0", "1", "2", "3", "4", "5", "6", "7", "8", "9");

prompt main {{
  is {{
    pin is text<length={length}, vocab=digit, stop="">;
  }}
  return {{
    use pin;
  }}
}}

export main;
'''


def _compile_pin(tmp_path, length):
    import autocog
    stl = tmp_path / "pin.stl"
    stl.write_text(EXACT_PIN.format(length=length))
    return autocog.compile(str(stl))


def test_exact_length_digits_rng(engine, tmp_path):
    """stop="" + digits vocab: exactly `length` digit tokens (RNG model)."""
    prog = _compile_pin(tmp_path, 4)
    result = engine.run(prog)
    assert len(result) == 4 and result.isdigit(), repr(result)


def test_exact_length_digits_real(real_engine, tmp_path):
    """Same guarantee under the real tokenizer: the mask admits only the ten
    single-digit tokens (plus nothing — no stop), so the completion is exactly
    `length` digits regardless of what the model would rather say."""
    prog = _compile_pin(tmp_path, 6)
    result = real_engine.run(prog)
    assert len(result) == 6 and result.isdigit(), repr(result)


def test_default_stop_still_terminates_rng(engine, tmp_path):
    """A restrictive vocab without stop="" must still terminate early: the
    syntax's stop token is unioned into the generation mask."""
    import autocog
    stl = tmp_path / "u.stl"
    stl.write_text('''
vocab digit = tokenize("0", "1", "2", "3", "4", "5", "6", "7", "8", "9");

prompt main {
  is {
    pin is text<length=40, vocab=digit>;
  }
  return {
    use pin;
  }
}

export main;
''')
    prog = autocog.compile(str(stl))
    result = engine.run(prog)
    # Under RNG the unioned newline is one of eleven admissible tokens, so a
    # 40-token budget stops early with overwhelming probability; digits-only
    # content either way.
    assert result.isdigit() or result == "", repr(result)
    assert len(result) < 40, f"stop token never sampled: {result!r}"
