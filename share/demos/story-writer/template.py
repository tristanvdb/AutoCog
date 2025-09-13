
templates = {
  "bedtime" : {
    "description" : [
      "Medium story to be read (many time) to young children.",
      "Might take half-hour to read. Might read different part each night.",
      "Nothing scary, it is bedtime. Avoid situation that could be stressful."
    ],
    "ages" : [3, 5],
    "sentence" : "simple with few preposition",
    "vocabulary" : "a couple uncommon word introduced by each story",
    "steps" : [
      {
        "name": "protagonist",
        "description" : "present the main character of the story",
        "pages" : [1,3]
        
      }, {
        "name": "perturbation"
        "description" : "the event that trigger the story",
        "pages" : [1,2]
      },{
        "name": "explorations",
        "description" : "protagonist tries to figure out a solution by trying all sort of things",
        "pages" : [5,20]
      },{
        "name": "resolution",
        "description" : "protagonist find the solution (or get help)",
        "pages" : [2,4]
      },{
        "name": "conclusion",
        "description" : "everything is back to normal but protagonist learned something",
        "pages" : [1,2]
      }
    ]
  },
  "wordbook" : {
    "description" : [
      "Short story that introduce simple word based on a theme",
      "Takes a few minutes to present to young kids",
      "Older kids can sounds the word by themself"
    ],
    "ages" : [1, 4],
    "sentence" : "single word, or very simple sentence: subject and verb maybe adjective",
    "vocabulary" : "all very common words",
    "steps" : [
      {
        "name": "theme",
        "description" : "illustrate the theme of the book",
        "pages" : [1,2]
        
      }, {
        "name": "words"
        "description" : "one word or concept per page",
        "pages" : [5,10]
      }
    ]
  }
}

def list_templates(age):
    return [
      { "template" : key, "description" : tpl['description'], "ages" : tpl['ages'] }
      for (key,tpl) in templates.items()
      if tpl['ages'][0] <= age and age <= tpl['ages'][1]
    ]

def open_template(key):
    return template[key]

def collate_task(query, age, key):
    res = [f"User ask for a book for children age {age}",f"User specified: \"{query}\""]
    res += ["Template description:"]
    res += [ f"> {d}" for d in templates[key]['description'] ]
    res += [f"Expected sentence style: {templates[key]['sentence']}"]
    res += [f"Expected vocabulary: {templates[key]['vocabulary']}"]
    res += ["Template steps:"]
    res += [ f"- {s['description']}" for s in templates[key]['steps'] ]
    return res

