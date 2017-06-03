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

function html_title(path)
	local f = io.open(path)
	if not f then return basename(path) else f:close() end
	for line in io.lines(path) do
		local match = line:match("<[Tt][Ii][Tt][Ll][Ee]>(.*)</[Tt][Ii][Tt][Ll][Ee]>")
		if match then return match end
	end
	return basename(path)
end

-- TODO Add an `iconv` function here that will convert encodings (by calling back to C++).  That way we can deprecate the `encoding` parameter
