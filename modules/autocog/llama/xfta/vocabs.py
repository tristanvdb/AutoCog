
import string
from .xfta_cxx import tokenize, detokenize, vocab_size

valid_char = (
    string.ascii_letters +      # a-z, A-Z
    string.digits +             # 0-9
    string.punctuation +        # .,!? etc.
    ' ' + '\n'                  # spaces
)

def create_safe_vocab_mask(model):
    vsize = vocab_size(model)
    mask = [True] * vsize

    for token_id in range(vsize):
        try:
            text = detokenize(model, [token_id], False, False)
            is_problematic = (
                '\n' in text or          # Newlines
                '\r' in text or          # Carriage returns  
                '\t' in text or          # Tabs
                len(text) == 0 or        # Empty tokens
                any(ord(c) < 32 for c in text) or  # Control characters
                any(ord(c) > 126 for c in text)  # Non-ASCII
            )

            if is_problematic and not text in valid_char:
                mask[token_id] = False
                
        except Exception:
            mask[token_id] = False

    return mask
    
def create_single_char_vocab(model):
    vsize = vocab_size(model)
    mask = [False] * vsize

    for char in valid_char:
        try:
            tokens = tokenize(model, char, False, False)
            if len(tokens) == 1:
                token_id = tokens[0]
                if 0 <= token_id < vsize:
                    mask[token_id] = True
        except Exception:
            pass
    
    return mask

