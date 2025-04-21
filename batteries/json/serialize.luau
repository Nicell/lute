--!optimize 2
--!native
local SPACE = 32
local NEWLINE = 10
local COMMA = 44
local QUOTATION = 34
local OPEN_BRACKET = 91
local CLOSE_BRACKET = 93
local OPEN_BRACE = 123
local CLOSE_BRACE = 125
local BACKSLASH = 92

-- used for string serialization
local ESCAPE_MAP = {
	[0x5C] = string.byte("\\"), -- 5C = '\'
	[0x08] = string.byte("b"),
	[0x0C] = string.byte("f"),
	[0x0A] = string.byte("n"),
	[0x0D] = string.byte("r"),
	[0x09] = string.byte("t"),
}

local types = require("../json/types")

local buff: buffer = buffer.create(1024)
local pretty = false
local size = 1024
local cursor = 0
local depth = 0

local function alloc(len: number)
	if cursor + len < size then
		return
	end

	size *= 2
	local newbuff = buffer.create(size)

	buffer.copy(newbuff, 0, buff)

	buff = newbuff
end

local function ALLOC_FOUR()
	if cursor + 4 < size then
		return
	end

	size *= 2
	local newbuff = buffer.create(size)

	buffer.copy(newbuff, 0, buff)

	buff = newbuff
end

local function WRITE_OPEN_BRACE()
	cursor += 1
	buffer.writeu8(buff, cursor, OPEN_BRACE)
end

local function WRITE_OPEN_BRACKET()
	cursor += 1
	buffer.writeu8(buff, cursor, OPEN_BRACKET)
end

local function WRITE_CLOSE_BRACKET()
	cursor += 1
	buffer.writeu8(buff, cursor, CLOSE_BRACKET)
end

local function WRITE_CLOSE_BRACE()
	cursor += 1
	buffer.writeu8(buff, cursor, CLOSE_BRACE)
end

local function WRITE_SPACE()
	cursor += 1
	buffer.writeu8(buff, cursor, SPACE)
end

local function WRITE_QUOTATION()
	cursor += 1
	buffer.writeu8(buff, cursor, QUOTATION)
end

local function WRITE_BACKSLASH()
	cursor += 1
	buffer.writeu8(buff, cursor, BACKSLASH)
end

local function WRITE_COMMA()
	cursor += 1
	buffer.writeu8(buff, cursor, COMMA)
end

local function WRITE_NEWLINE()
	cursor += 1
	buffer.writeu8(buff, cursor, NEWLINE)
end

local function writeSpaces()
	if depth == 0 then
		return
	end

	if pretty then
		alloc(depth * 4)

		for i = 0, depth do
			cursor += 4
			buffer.writeu32(buff, cursor, 0x_20_20_20_20)
		end
	else
		ALLOC_FOUR()
		cursor += 1
		buffer.writeu8(buff, cursor, SPACE)
	end
end

local function writeString(str: string)
	alloc(#str)

	cursor += 1

	buffer.writestring(buff, cursor, str)

	cursor += #str
end

local serializeAny: (value: types.Value) -> ()

local function serializeUnicode(codepoint: number)
	if codepoint >= 0x10000 then
		local high = math.floor((codepoint - 0x10000) / 0x400) + 0xD800
		local low = ((codepoint - 0x10000) % 0x400) + 0xDC00
		return string.format("\\u%04x\\u%04x", high, low)
	end

	return string.format("\\u%04x", codepoint)
end

local function serializeString(str: string)
	-- covers the quotations & utf
	alloc((#str * 4) + 2)

	WRITE_QUOTATION()

	for pos, codepoint in utf8.codes(str) do
		if ESCAPE_MAP[codepoint] then
			WRITE_BACKSLASH()
			cursor += 1
			buffer.writeu8(buff, cursor, ESCAPE_MAP[codepoint])
		elseif codepoint < 32 or codepoint > 126 then
			writeString(serializeUnicode(codepoint))
		else
			-- we are in ascii
			cursor += 1
			buffer.writeu8(buff, cursor, codepoint)
		end
	end

	WRITE_QUOTATION()
end

local function serializeArray(array: types.Array)
	depth += 1

	ALLOC_FOUR()
	WRITE_OPEN_BRACKET()

	if pretty and #array ~= 0 then
		WRITE_NEWLINE()
	end

	for i, value in array do
		if i ~= 1 then
			ALLOC_FOUR()
			WRITE_COMMA()

			if pretty then
				WRITE_NEWLINE()
			end
		end

		if i ~= 1 or pretty then
			ALLOC_FOUR()
			if pretty then
				writeSpaces()
			end
		end

		serializeAny(value)
	end

	depth -= 1

	if pretty and #array ~= 0 then
		WRITE_NEWLINE()
		if pretty then
			writeSpaces()
		end
	end

	WRITE_CLOSE_BRACKET()
end

local function serializeTable(object: types.Object)
	ALLOC_FOUR()
	WRITE_OPEN_BRACE()

	if pretty then
		WRITE_NEWLINE()
	end

	depth += 1

	local first = true
	for key, value in object do
		if not first then
			ALLOC_FOUR()
			WRITE_COMMA()

			if pretty then
				WRITE_NEWLINE()
			end
		end

		first = false

		ALLOC_FOUR()

		WRITE_QUOTATION()
		cursor += 1
		buffer.writestring(buff, cursor, key)
		cursor += #key

		buffer.writestring(buff, cursor, '":')
		cursor += 2

		if pretty then
			writeSpaces()
		end

		serializeAny(value)

		if pretty then
			writeSpaces()
		end
	end

	if pretty then
		WRITE_NEWLINE()
	end

	depth -= 1

	if pretty then
		writeSpaces()
	end

	WRITE_CLOSE_BRACE()
	cursor += 1
end

serializeAny = function(value: types.Value)
	local valueType = type(value)

	if value == types.NULL then
		writeString("null")
	elseif valueType == "boolean" then
		writeString(if value then "true" else "false")
	elseif valueType == "number" then
		writeString(tostring(value))
	elseif valueType == "string" then
		serializeString(value :: string)
	elseif valueType == "table" then
		if #(value :: {}) == 0 and next(value :: any) ~= nil then
			serializeTable(value :: types.Object)
		else
			serializeArray(value :: types.Array)
		end
	else
		error("Unknown value", 2)
	end
end

local function serialize(data: types.Value, shouldBePretty: boolean)
	buff = buffer.create(1024)
	size = 1024
	cursor = 0
	pretty = shouldBePretty

	serializeAny(data)

	return buffer.readstring(buff, 0, cursor)
end

return serialize
