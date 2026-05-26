
import sys
import autocog.llama

# Create both implementations
model = autocog.llama.create(sys.argv[1], 4096)

# Test tokenization round-trip
test_texts = ["Hello world", "This is a test", "System prompt with\nmultiple lines"]

for text in test_texts:
    cpp_tokens = autocog.llama.tokenize(model, text, False, False)
    cpp_reconstructed = autocog.llama.detokenize(model, cpp_tokens, False, False)
    assert text == cpp_reconstructed, f"Round-trip failed: {text} vs {cpp_reconstructed}"

print("All tokenization tests passed!")
