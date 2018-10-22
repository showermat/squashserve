T_UNK = 0; T_DIR = 1; T_REG = 2; T_LNK = 3

function basename(path)
	local match = path:match(".*/([^/]*)$")
	local fname
	if match then fname = match else fname = path end
	local match = fname:match("^(.*)%.")
	local base
	if match then base = match else base = fname end
	return base
end

function extension(path)
	local match = path:match(".*%.(.-)$")
	if match then return match else return path end
end

function is_html(path)
	return extension(path) == "html" or extension(path) == "htm"
end

-- Functions registered from C++:
--     iconv(in, from, to): Convert the encoding of a string
--     mimetype(path): Return the MIME type of a file using its extension if possible or libmagic otherwise
--     html_title(path): Retrieve the title from an HTML document
--     html_encoding(path): Retrieve the declared encoding from an HTML document, or "latin-1" if missing

function default_meta(path, ftype)
	ret = {}
	if ftype == T_REG or ftype == T_LNK then
		if is_html(path) then ret["title"] = html_title(path) end
		ret["type"] = mimetype(path)
	end
	return ret
end

function default_meta_trim(path, ftype, pattern)
	local ret = default_meta(path, ftype)
	if ret["title"] then
		ret["title"] = ret["title"]:match(pattern) or ret["title"]
	end
	return ret
end
