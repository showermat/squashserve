#! /usr/bin/python

import sys;
import os;
import lzma;
import struct;

class Entry:
	def init_basic(self, fid, name, parent, start, length):
		self.fid = fid;
		self.name = name;
		self.parent = parent;
		self.start = start;
		self.length = length;
		return self;
	def init_binary(self, data):
		(self.fid, self.parent, self.start, self.length, namelen) = struct.unpack("<QQQQh", data[0:34]);
		self.name = data[34:namelen+34].decode("UTF-8");
		return namelen + 34;
	def serialize(self):
		namebytes = self.name.encode("UTF-8");
		return struct.pack("<QQQQh", self.fid, self.parent, self.start, self.length, len(namebytes)) + namebytes;
	def printout(self):
		print("%d\t%d\t%d\t%d\t%s" % (self.fid, self.parent, self.start, self.length, self.name));

class Node:
	def __init__(self, name, start, length):
		self.name = name;
		self.start = start;
		self.length = length;
		self.children = [];

class BadZsr(Exception):
	def __init__(self, msg): super(BadZsr, self).__init__(msg);

class Zsr:
	def recursive_add(self, path, fid, out):
		for name in os.listdir(path):
			self.fidcnt += 1;
			newpath = os.path.join(path, name);
			if (os.path.isfile(newpath)):
				data = lzma.compress(open(newpath, 'rb').read());
				out.write(data);
				start = self.fileloc;
				length = len(data);
				self.fileloc += length;
				self.index.append(Entry().init_basic(self.fidcnt, name, fid, start, length));
			elif (os.path.isdir(newpath)):
				self.index.append(Entry().init_basic(self.fidcnt, name, fid, 0, 0));
				self.recursive_add(newpath, self.fidcnt, out);
			else: print("Warning: Skipping non-normal file " + newpath);

	def recursive_restore(self, node, inf):
		for childidx in node.children:
			child = self.tree[childidx];
			if child.start == 0:
				os.mkdir(child.name);
				os.chdir(child.name);
				self.recursive_restore(child, inf);
				os.chdir("..");
			else:
				inf.seek(child.start);
				out = open(child.name, 'wb');
				try: out.write(lzma.decompress(inf.read(child.length)));
				except lzma.LZMAError as e: print("Couldn't extract " + os.path.join(os.getcwd(), child.name) + ": " + str(e));
				out.close();

	def treeprint(self, idx, indent):
		print(indent + self.tree[idx].name);
		for child in self.tree[idx].children: self.treeprint(child, indent + "  ");

	def __init__(self):
		self.index = [];
		self.fidcnt = 0;
		self.fileloc = 0;
		self.tree = dict();

	def create(self, path, outf): # Creates an archive from a file or directory
		out = open(outf, 'wb');
		oldwd = os.getcwd();
		os.chdir(path);
		out.write(b"!ZSR\x00\x00\x00\x00\x00\x00\x00\x00");
		self.fileloc = 12;
		self.recursive_add(".", 0, out);
		idxbin = b"";
		for ent in self.index: idxbin += ent.serialize();
		out.write(lzma.compress(idxbin));
		out.seek(4);
		out.write(struct.pack("<Q", self.fileloc));
		out.close();
		os.chdir(oldwd);
		#for ent in self.index: ent.printout();

	def extract(self, dest): # Extracts the contents of the archive to the given path
		os.mkdir(dest);
		os.chdir(dest);
		self.recursive_restore(self.tree[0], self.inf);

	def read(self, path): # Reads an archive from a file
		self.inf = open(path, 'rb');
		try: (magic, idxstart) = struct.unpack("<4sQ", self.inf.read(12));
		except Exception: raise BadZsr("File too small");
		if magic != b"!ZSR": raise BadZsr("Bad magic number");
		try:
			self.inf.seek(idxstart);
			idxbin = lzma.decompress(self.inf.read());
		except Exception: raise BadZsr("Could not retreive index");
		idxcnt = 0;
		self.tree[0] = Node(".", 0, 0);
		while idxcnt < len(idxbin):
			ent = Entry();
			idxcnt += ent.init_binary(idxbin[idxcnt:]);
			self.index.append(ent);
			self.tree[ent.fid] = Node(ent.name, ent.start, ent.length);
			self.tree[ent.parent].children.append(ent.fid);
		#for ent in self.index: ent.printout();
		#self.treeprint(0, "");

	def node(self, path): # Gets the node corresponding to the given path.  Internal use only
		cur = self.tree[0];
		for elem in path.split("/"):
			oldcur = cur;
			for childidx in cur.children:
				if self.tree[childidx].name == elem:
					cur = self.tree[childidx];
					break;
			if oldcur == cur: return None;
		return cur;

	def check(self, path): # Checks for the existence of a file in the archive
		cur = self.node(path);
		if cur is None: return False;
		if cur.start == 0: return False;
		return True;

	def get(self, path): # Returns the content of a single archived file
		cur = self.node(path);
		if cur is None: raise KeyError("Requested file does not exist");
		if cur.start == 0: raise KeyError("Cannot extract directory");
		self.inf.seek(cur.start);
		return lzma.decompress(self.inf.read(cur.length));

	def getdir(self, path, dest): # Retrieve the contents of a directory
		cur = self.node(path);
		if cur is None: raise KeyError("Requested directory does not exists");
		if cur.start != 0: raise KeyError("Cannot extract contents of regular file");
		if not os.path.isdir(dest): os.mkdir(dest);
		oldwd = os.getcwd();
		os.chdir(dest);
		self.recursive_restore(cur, self.inf);
		os.chdir(oldwd);

if __name__ == "__main__":
	if sys.argv[1] == 'c':
		Zsr().create(sys.argv[2], sys.argv[3]);
	elif sys.argv[1] == 'x':
		zsr = Zsr();
		zsr.read(sys.argv[2]);
		if len(sys.argv) >= 5: open(sys.argv[3], 'wb').write(zsr.get(sys.argv[4]));
		else: zsr.extract(sys.argv[3]);
	elif sys.argv[1] == 't':
		zsr = Zsr();
		zsr.read(sys.argv[2]);
		zsr.getdir(sys.argv[3], sys.argv[4]);
	else:
		raise RuntimeError("Usage: c <dir> <dest> | x <archive>");

