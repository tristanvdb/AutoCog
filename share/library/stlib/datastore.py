
_store = {}

def store(data, pkey="", skey=""):
    global _store
    if pkey not in _store:
        _store[pkey] = { skey : data }
    else:
        _store[pkey][skey] = data
    return { "pkey" : pkey, "skey" : skey }

def retrieve(pkey="", skey=""):
    global _store
    if pkey not in _store:
        return None
    elif skey not in _store[pkey]:
        return None
    return _store[pkey][skey]
