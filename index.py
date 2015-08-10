#! /usr/bin/python

import sys;
import os;
import re;
import mimetypes;
import xapian;

def encoding(path):
	content = open(path, "rb").read().decode("UTF-8", "ignore");
	match = re.search("charset=([^\">]+[\">])", content);
	if match: return match.group(1);
	return "UTF-8";

root = sys.argv[1];
if root[-1] == "/": root = root[:-1];
db = xapian.WritableDatabase(os.path.join(root, "_meta", "index"));
indexer = xapian.TermGenerator();
indexer.set_stemmer(xapian.Stem("english"));

i = 0;
for (parent, dirs, files) in os.walk(root):
	relpath = parent[len(root)+1:]
	if relpath == "": dirs.remove("_meta");
	for sfile in files:
		mtype = mimetypes.guess_type(sfile)[0];
		if mtype is None: continue;
		if mtype.split("/")[0] != "text": continue;
		fpath = os.path.join(relpath, sfile);
		abspath = os.path.join(parent, sfile);
		if mtype != "text/html": content = open(abspath, "rb").read().decode("UTF-8", "ignore");
		else:
			try: content = open(abspath, "rb").read().decode(encoding(abspath), "ignore");
			except LookupError: content = open(abspath, "rb").read().decode("UTF-8", "ignore");
		doc = xapian.Document();
		doc.set_data(fpath);
		indexer.set_document(doc);
		indexer.index_text(content);
		db.add_document(doc);
		i += 1;
		print("\r\033[K%i  %s" % (i, abspath), end="");
print();
db.close();

