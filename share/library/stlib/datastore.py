
data = {}

def store(data, pkey="", skey=""):
    global data
    if not pkey in data:
    	data.update({ pkey : { skey : data } })
    else:
    	data[pkey].update({ skey : data })
    return { "pkey" : pkey,  "skey" : skey  }

def retrieve(pkey="", skey=""):
    global data
    if not pkey in data:
    	return None
    elif not skey in data[pkey]:
    	return None
    return data[pkey][skey]

