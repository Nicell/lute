--!native
local SPACE = 32
local NEWLINE = 10
local COMMA = 44
local QUOTATION = 34
local COLON = 58
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

local types = require("./types")

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
	local newBuffer = buffer.create(size)

	buffer.copy(newBuffer, 0, buff)

	buff = newBuffer
end

local function writeByte(byte: number)
	buffer.writeu8(buff, cursor, byte)

	cursor += 1
end

local function writeSpaces()
	if depth == 0 then
		return
	end

	if pretty then
		alloc(depth * 4)

		for i = 0, depth do
			buffer.writeu32(buff, cursor, 0x_20_20_20_20)
			cursor += 4
		end
	else
		alloc(1)
		buffer.writeu8(buff, cursor, SPACE)

		cursor += 1
	end
end

local function writeString(str: string)
	alloc(#str)

	buffer.writestring(buff, cursor, str)

	cursor += #str
end

local serializeAny: (value: types.Value) -> string

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

	writeByte(QUOTATION)

	for pos, codepoint in utf8.codes(str) do
		if ESCAPE_MAP[codepoint] then
			writeByte(BACKSLASH)
			writeByte(ESCAPE_MAP[codepoint])
		elseif codepoint < 32 or codepoint > 126 then
			writeString(serializeUnicode(codepoint))
		else
			writeString(utf8.char(codepoint))
		end
	end

	writeByte(QUOTATION)
end

local function serializeArray(array: types.Array)
	depth += 1

	alloc(1)
	writeByte(OPEN_BRACKET)

	if pretty and #array ~= 0 then
		alloc(1)
		writeByte(NEWLINE)
	end

	for i, value in array do
		if i ~= 1 then
			alloc(1)
			writeByte(COMMA)

			if pretty then
				alloc(1)
				writeByte(NEWLINE)
			end
		end

		if i ~= 1 or pretty then
			alloc(1)
			writeSpaces()
		end

		serializeAny(value)
	end

	depth -= 1

	if pretty and #array ~= 0 then
		alloc(1)
		writeByte(NEWLINE)
		writeSpaces()
	end

	alloc(1)
	writeByte(CLOSE_BRACKET)
end

local function serializeTable(object: types.Object)
	alloc(1)
	writeByte(OPEN_BRACE)

	if pretty then
		alloc(1)
		writeByte(NEWLINE)
	end

	depth += 1

	local first = true
	for key, value in object do
		if not first then
			alloc(2)
			writeByte(COMMA)
			writeByte(SPACE)

			if pretty then
				alloc(1)
				writeByte(NEWLINE)
			end
		end

		first = false

		writeSpaces()

		alloc(4)
		writeByte(QUOTATION)
		writeString(key)
		writeByte(QUOTATION)
		writeByte(COLON)
		writeByte(SPACE)

		serializeAny(value)
	end

	if pretty then
		alloc(1)
		writeByte(NEWLINE)
	end

	depth -= 1

	writeSpaces()

	alloc(1)
	writeByte(CLOSE_BRACE)
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

	local stringbuff = buffer.create(cursor)
	buffer.copy(stringbuff, 0, buff, 0, cursor)

	return buffer.tostring(stringbuff)
end

return serializeAny
