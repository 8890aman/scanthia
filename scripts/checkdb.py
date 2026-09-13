import sqlite3, json, os
db = sqlite3.connect(r'C:\Users\Administrator\AppData\Roaming\Scanthia\Scanthia\Scanthia.db')
row = db.execute("SELECT files FROM series WHERE description='Angio THIN'").fetchone()
f = json.loads(row[0])
print('count:', len(f))
print('first:', repr(f[0]))
print('exists:', os.path.exists(f[0]))
print('empty entries:', sum(1 for x in f if not x))
print('missing:', sum(1 for x in f if not os.path.exists(x)))
