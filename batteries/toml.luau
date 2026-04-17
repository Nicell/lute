--!strict
--!optimize 2
--[[
	@batteries/toml
	TOML v1.0.0 compliant parser and serializer.

	Reference: https://toml.io/en/v1.0.0

	Datetime-typed values (offset-datetime, local-datetime, local-date, local-time)
	have no native equivalent in Luau, so they are represented as tables tagged
	with a `_type` field. Use the provided constructors (`toml.datetime`, ...) to
	build them, or rely on the parser to produce them for you. The serializer
	recognizes these tagged tables and emits the appropriate TOML form.
]]

local toml = {}

-- ===== Public types =====

export type OffsetDateTime = {
	_type: "datetime",
	year: number,
	month: number,
	day: number,
	hour: number,
	minute: number,
	second: number,
	nanosecond: number,
	offset: string, -- "Z" or "+HH:MM" or "-HH:MM"
}

export type LocalDateTime = {
	_type: "datetime-local",
	year: number,
	month: number,
	day: number,
	hour: number,
	minute: number,
	second: number,
	nanosecond: number,
}

export type LocalDate = {
	_type: "date-local",
	year: number,
	month: number,
	day: number,
}

export type LocalTime = {
	_type: "time-local",
	hour: number,
	minute: number,
	second: number,
	nanosecond: number,
}

export type DateTimeLike = OffsetDateTime | LocalDateTime | LocalDate | LocalTime
export type Array = { Value }
export type Table = { [string]: Value }
export type Value = string | number | boolean | DateTimeLike | Array | Table

-- ===== Constructors =====

function toml.datetime(t: {
	year: number,
	month: number,
	day: number,
	hour: number,
	minute: number,
	second: number,
	nanosecond: number?,
	offset: string,
}): OffsetDateTime
	return {
		_type = "datetime",
		year = t.year,
		month = t.month,
		day = t.day,
		hour = t.hour,
		minute = t.minute,
		second = t.second,
		nanosecond = t.nanosecond or 0,
		offset = t.offset,
	}
end

function toml.localDatetime(t: {
	year: number,
	month: number,
	day: number,
	hour: number,
	minute: number,
	second: number,
	nanosecond: number?,
}): LocalDateTime
	return {
		_type = "datetime-local",
		year = t.year,
		month = t.month,
		day = t.day,
		hour = t.hour,
		minute = t.minute,
		second = t.second,
		nanosecond = t.nanosecond or 0,
	}
end

function toml.date(t: { year: number, month: number, day: number }): LocalDate
	return { _type = "date-local", year = t.year, month = t.month, day = t.day }
end

function toml.time(t: { hour: number, minute: number, second: number, nanosecond: number? }): LocalTime
	return {
		_type = "time-local",
		hour = t.hour,
		minute = t.minute,
		second = t.second,
		nanosecond = t.nanosecond or 0,
	}
end

-- ===== Parser =====

type ParserState = {
	src: string,
	len: number,
	pos: number,
	line: number,
	col: number,
	root: { [string]: any },
	-- Per-table metadata, indexed by table identity.
	explicit: { [any]: boolean }, -- defined by a `[x.y]` header
	inline: { [any]: boolean }, -- defined as an inline `{}` table (immutable after creation)
	dotted: { [any]: boolean }, -- created as an intermediate of a dotted key (cannot be redefined)
	arrays: { [any]: boolean }, -- value is an array-of-tables (the array itself)
	-- Dotted-key intermediates that can still be extended by further dotted keys
	-- within the current `[table]` scope.
	dottedOpen: { [any]: boolean },
}

local function throwError(state: ParserState, msg: string)
	error(string.format("TOML parse error at line %d, column %d: %s", state.line, state.col, msg), 0)
end

local function peekByte(state: ParserState, offset: number?): number
	local p = state.pos + (offset or 0)
	if p > state.len then
		return -1
	end
	return (string.byte(state.src, p))
end

local function eof(state: ParserState): boolean
	return state.pos > state.len
end

-- Advance N bytes, updating line/column counters.
local function advanceBytes(state: ParserState, n: number): ()
	local s = state.src
	for i = 0, n - 1 do
		local b = string.byte(s, state.pos + i)
		if b == 10 then
			state.line += 1
			state.col = 1
		else
			state.col += 1
		end
	end
	state.pos += n
end

-- Skip space and tab only (not newlines).
local function skipWhitespace(state: ParserState): ()
	local s = state.src
	while state.pos <= state.len do
		local b = string.byte(s, state.pos)
		if b == 0x20 or b == 0x09 then
			state.pos += 1
			state.col += 1
		else
			break
		end
	end
end

-- Consume a comment from `#` to end of line (not including the newline).
local function skipComment(state: ParserState): ()
	if peekByte(state) ~= 0x23 then
		return
	end
	state.pos += 1
	state.col += 1
	local s = state.src
	while state.pos <= state.len do
		local b = string.byte(s, state.pos)
		if b == 0x0A then
			return
		end
		if b == 0x0D and peekByte(state, 1) == 0x0A then
			return
		end
		if (b < 0x20 and b ~= 0x09) or b == 0x7F then
			throwError(state, string.format("invalid control character (U+%04X) in comment", b))
		end
		state.pos += 1
		state.col += 1
	end
end

-- After a value or table header: allow trailing whitespace/comment, then require a newline (or EOF).
local function requireLineEnd(state: ParserState): ()
	skipWhitespace(state)
	skipComment(state)
	if eof(state) then
		return
	end
	local b = peekByte(state)
	if b == 0x0A then
		advanceBytes(state, 1)
	elseif b == 0x0D then
		if peekByte(state, 1) == 0x0A then
			advanceBytes(state, 2)
		else
			throwError(state, "bare carriage return not allowed")
		end
	else
		throwError(state, "expected newline or end of input")
	end
end

-- ===== Keys =====

local function isBareKeyChar(b: number): boolean
	return (b >= 0x41 and b <= 0x5A) -- A-Z
		or (b >= 0x61 and b <= 0x7A) -- a-z
		or (b >= 0x30 and b <= 0x39) -- 0-9
		or b == 0x5F -- _
		or b == 0x2D -- -
end

local parseBasicString: (ParserState) -> string
local parseLiteralString: (ParserState) -> string

local function parseBareKey(state: ParserState): string
	local start = state.pos
	local s = state.src
	while state.pos <= state.len and isBareKeyChar(string.byte(s, state.pos)) do
		state.pos += 1
		state.col += 1
	end
	if state.pos == start then
		throwError(state, "expected key")
	end
	return string.sub(s, start, state.pos - 1)
end

local function parseKeySegment(state: ParserState): string
	local b = peekByte(state)
	if b == 0x22 then
		return parseBasicString(state)
	elseif b == 0x27 then
		return parseLiteralString(state)
	elseif isBareKeyChar(b) then
		return parseBareKey(state)
	else
		throwError(state, "expected key")
		error("unreachable")
	end
end

local function parseKey(state: ParserState): { string }
	local parts: { string } = {}
	table.insert(parts, parseKeySegment(state))
	skipWhitespace(state)
	while peekByte(state) == 0x2E do
		state.pos += 1
		state.col += 1
		skipWhitespace(state)
		table.insert(parts, parseKeySegment(state))
		skipWhitespace(state)
	end
	return parts
end

-- ===== Strings =====

local function encodeUtf8(cp: number): string
	if cp < 0x80 then
		return string.char(cp)
	elseif cp < 0x800 then
		return string.char(0xC0 + math.floor(cp / 64), 0x80 + cp % 64)
	elseif cp < 0x10000 then
		return string.char(
			0xE0 + math.floor(cp / 4096),
			0x80 + math.floor(cp / 64) % 64,
			0x80 + cp % 64
		)
	else
		return string.char(
			0xF0 + math.floor(cp / 262144),
			0x80 + math.floor(cp / 4096) % 64,
			0x80 + math.floor(cp / 64) % 64,
			0x80 + cp % 64
		)
	end
end

local function isHexDigit(b: number): boolean
	return (b >= 0x30 and b <= 0x39) or (b >= 0x41 and b <= 0x46) or (b >= 0x61 and b <= 0x66)
end

local function parseUnicodeEscape(state: ParserState, width: number): string
	if state.pos + width - 1 > state.len then
		throwError(state, "incomplete unicode escape")
	end
	for i = 0, width - 1 do
		if not isHexDigit(string.byte(state.src, state.pos + i)) then
			throwError(state, "invalid unicode escape")
		end
	end
	local hex = string.sub(state.src, state.pos, state.pos + width - 1)
	local cp = tonumber(hex, 16) :: number
	if cp > 0x10FFFF or (cp >= 0xD800 and cp <= 0xDFFF) then
		throwError(state, "invalid unicode codepoint")
	end
	advanceBytes(state, width)
	return encodeUtf8(cp)
end

local function parseEscapeSequence(state: ParserState): string
	local b = peekByte(state)
	if b == -1 then
		throwError(state, "unterminated escape sequence")
	end
	state.pos += 1
	state.col += 1
	if b == 0x62 then
		return "\b"
	elseif b == 0x74 then
		return "\t"
	elseif b == 0x6E then
		return "\n"
	elseif b == 0x66 then
		return "\f"
	elseif b == 0x72 then
		return "\r"
	elseif b == 0x22 then
		return '"'
	elseif b == 0x5C then
		return "\\"
	elseif b == 0x75 then
		return parseUnicodeEscape(state, 4)
	elseif b == 0x55 then
		return parseUnicodeEscape(state, 8)
	else
		throwError(state, string.format("invalid escape sequence: \\%s", string.char(b)))
		error("unreachable")
	end
end

function parseBasicString(state: ParserState): string
	advanceBytes(state, 1) -- opening "
	local parts: { string } = {}
	local runStart = state.pos
	local s = state.src
	while true do
		if state.pos > state.len then
			throwError(state, "unterminated string")
		end
		local b = string.byte(s, state.pos)
		if b == 0x22 then
			if state.pos > runStart then
				table.insert(parts, string.sub(s, runStart, state.pos - 1))
			end
			state.pos += 1
			state.col += 1
			return table.concat(parts)
		elseif b == 0x5C then
			if state.pos > runStart then
				table.insert(parts, string.sub(s, runStart, state.pos - 1))
			end
			state.pos += 1
			state.col += 1
			table.insert(parts, parseEscapeSequence(state))
			runStart = state.pos
		elseif b == 0x0A or b == 0x0D then
			throwError(state, "newline not allowed in basic string")
		elseif (b < 0x20 and b ~= 0x09) or b == 0x7F then
			throwError(state, string.format("invalid control character (U+%04X) in string", b))
		else
			state.pos += 1
			state.col += 1
		end
	end
end

function parseLiteralString(state: ParserState): string
	advanceBytes(state, 1) -- opening '
	local start = state.pos
	local s = state.src
	while true do
		if state.pos > state.len then
			throwError(state, "unterminated literal string")
		end
		local b = string.byte(s, state.pos)
		if b == 0x27 then
			local out = string.sub(s, start, state.pos - 1)
			state.pos += 1
			state.col += 1
			return out
		elseif b == 0x0A or b == 0x0D then
			throwError(state, "newline not allowed in literal string")
		elseif (b < 0x20 and b ~= 0x09) or b == 0x7F then
			throwError(state, string.format("invalid control character (U+%04X) in string", b))
		else
			state.pos += 1
			state.col += 1
		end
	end
end

local function consumeLineEndingBackslash(state: ParserState): boolean
	-- Called at position of `\`. Returns true if this `\` is a line-ending backslash
	-- (i.e. followed only by whitespace/tabs until newline). Consumes through next non-whitespace.
	local s = state.src
	local p = state.pos + 1
	while p <= state.len do
		local b = string.byte(s, p)
		if b == 0x20 or b == 0x09 then
			p += 1
		else
			break
		end
	end
	if p > state.len then
		return false
	end
	local nb = string.byte(s, p)
	if nb ~= 0x0A and nb ~= 0x0D then
		return false
	end
	-- Confirmed: consume backslash + whitespace + newline + all following whitespace.
	advanceBytes(state, 1) -- the `\`
	while state.pos <= state.len do
		local b = string.byte(s, state.pos)
		if b == 0x20 or b == 0x09 or b == 0x0A then
			advanceBytes(state, 1)
		elseif b == 0x0D then
			if peekByte(state, 1) == 0x0A then
				advanceBytes(state, 2)
			else
				throwError(state, "bare carriage return not allowed")
			end
		else
			break
		end
	end
	return true
end

local function parseMultilineBasicString(state: ParserState): string
	advanceBytes(state, 3) -- opening """
	-- Trim a single immediate newline.
	if peekByte(state) == 0x0A then
		advanceBytes(state, 1)
	elseif peekByte(state) == 0x0D and peekByte(state, 1) == 0x0A then
		advanceBytes(state, 2)
	end
	local parts: { string } = {}
	local runStart = state.pos
	local s = state.src
	local function flushRun()
		if state.pos > runStart then
			table.insert(parts, string.sub(s, runStart, state.pos - 1))
		end
	end
	while true do
		if state.pos > state.len then
			throwError(state, "unterminated multi-line string")
		end
		local b = string.byte(s, state.pos)
		if b == 0x22 then
			-- Count consecutive quotes.
			local n = 1
			while n < 6 and state.pos + n <= state.len and string.byte(s, state.pos + n) == 0x22 do
				n += 1
			end
			if n >= 3 then
				flushRun()
				if n > 5 then
					throwError(state, 'too many consecutive " in multi-line basic string')
				end
				for _ = 1, n - 3 do
					table.insert(parts, '"')
				end
				advanceBytes(state, n)
				return table.concat(parts)
			else
				advanceBytes(state, n)
			end
		elseif b == 0x5C then
			flushRun()
			if consumeLineEndingBackslash(state) then
				runStart = state.pos
			else
				advanceBytes(state, 1)
				table.insert(parts, parseEscapeSequence(state))
				runStart = state.pos
			end
		elseif b == 0x0D then
			if peekByte(state, 1) == 0x0A then
				advanceBytes(state, 1) -- we'll include the \n, drop the \r
				-- To avoid slicing issues, flush and continue.
				flushRun()
				table.insert(parts, "\n")
				advanceBytes(state, 1)
				runStart = state.pos
			else
				throwError(state, "bare carriage return not allowed in multi-line string")
			end
		elseif b == 0x0A then
			advanceBytes(state, 1)
		elseif (b < 0x20 and b ~= 0x09) or b == 0x7F then
			throwError(state, string.format("invalid control character (U+%04X) in string", b))
		else
			state.pos += 1
			state.col += 1
		end
	end
end

local function parseMultilineLiteralString(state: ParserState): string
	advanceBytes(state, 3) -- opening '''
	if peekByte(state) == 0x0A then
		advanceBytes(state, 1)
	elseif peekByte(state) == 0x0D and peekByte(state, 1) == 0x0A then
		advanceBytes(state, 2)
	end
	local parts: { string } = {}
	local runStart = state.pos
	local s = state.src
	local function flushRun()
		if state.pos > runStart then
			-- Normalize \r\n -> \n by filtering (rare path).
			local chunk = string.sub(s, runStart, state.pos - 1)
			if string.find(chunk, "\r", 1, true) then
				chunk = string.gsub(chunk, "\r\n", "\n")
			end
			table.insert(parts, chunk)
		end
	end
	while true do
		if state.pos > state.len then
			throwError(state, "unterminated multi-line literal string")
		end
		local b = string.byte(s, state.pos)
		if b == 0x27 then
			local n = 1
			while n < 6 and state.pos + n <= state.len and string.byte(s, state.pos + n) == 0x27 do
				n += 1
			end
			if n >= 3 then
				flushRun()
				if n > 5 then
					throwError(state, "too many consecutive ' in multi-line literal string")
				end
				for _ = 1, n - 3 do
					table.insert(parts, "'")
				end
				advanceBytes(state, n)
				return table.concat(parts)
			else
				advanceBytes(state, n)
			end
		elseif b == 0x0D then
			if peekByte(state, 1) ~= 0x0A then
				throwError(state, "bare carriage return not allowed in multi-line string")
			end
			advanceBytes(state, 1) -- leave \n for run
		elseif b == 0x0A then
			advanceBytes(state, 1)
		elseif (b < 0x20 and b ~= 0x09) or b == 0x7F then
			throwError(state, string.format("invalid control character (U+%04X) in string", b))
		else
			state.pos += 1
			state.col += 1
		end
	end
end

-- ===== Numbers =====

local function isDecDigit(b: number): boolean
	return b >= 0x30 and b <= 0x39
end
local function isOctDigit(b: number): boolean
	return b >= 0x30 and b <= 0x37
end
local function isBinDigit(b: number): boolean
	return b == 0x30 or b == 0x31
end

-- Consume a sequence of valid digits, optionally separated by single underscores.
-- Underscores must be surrounded by digits on both sides.
local function parseDigits(state: ParserState, isValid: (number) -> boolean, label: string): string
	local out: { string } = {}
	local prevDigit = false
	local hasAny = false
	local s = state.src
	while state.pos <= state.len do
		local b = string.byte(s, state.pos)
		if isValid(b) then
			table.insert(out, string.char(b))
			state.pos += 1
			state.col += 1
			prevDigit = true
			hasAny = true
		elseif b == 0x5F then
			if not prevDigit then
				throwError(state, "underscore must be surrounded by digits in " .. label)
			end
			state.pos += 1
			state.col += 1
			prevDigit = false
		else
			break
		end
	end
	if not hasAny then
		throwError(state, "expected " .. label)
	end
	if not prevDigit then
		throwError(state, "underscore must be surrounded by digits in " .. label)
	end
	return table.concat(out)
end

local function parseRadixInt(state: ParserState, radix: number, isValid: (number) -> boolean, label: string): number
	local digits = parseDigits(state, isValid, label)
	local n = tonumber(digits, radix)
	if not n then
		throwError(state, "invalid " .. label)
	end
	return n :: number
end

-- Returns value, isFloat.
local function parseNumberValue(state: ParserState): (number, boolean)
	local sign = 1
	local signStr = ""
	local b = peekByte(state)
	if b == 0x2B then
		signStr = "+"
		state.pos += 1
		state.col += 1
	elseif b == 0x2D then
		signStr = "-"
		sign = -1
		state.pos += 1
		state.col += 1
	end

	b = peekByte(state)
	if b == 0x69 then
		if string.sub(state.src, state.pos, state.pos + 2) == "inf" then
			advanceBytes(state, 3)
			return sign * math.huge, true
		end
		throwError(state, "unexpected input")
	elseif b == 0x6E then
		if string.sub(state.src, state.pos, state.pos + 2) == "nan" then
			advanceBytes(state, 3)
			return 0 / 0, true
		end
		throwError(state, "unexpected input")
	end

	if signStr == "" and b == 0x30 then
		local nb = peekByte(state, 1)
		if nb == 0x78 then
			advanceBytes(state, 2)
			return parseRadixInt(state, 16, isHexDigit, "hex integer"), false
		elseif nb == 0x6F then
			advanceBytes(state, 2)
			return parseRadixInt(state, 8, isOctDigit, "octal integer"), false
		elseif nb == 0x62 then
			advanceBytes(state, 2)
			return parseRadixInt(state, 2, isBinDigit, "binary integer"), false
		end
	end

	local intPart = parseDigits(state, isDecDigit, "decimal digits")
	if #intPart > 1 and string.sub(intPart, 1, 1) == "0" then
		throwError(state, "leading zero not allowed")
	end

	local isFloat = false
	local fracPart: string? = nil
	local expPart: string? = nil
	local expSign = ""

	if peekByte(state) == 0x2E then
		isFloat = true
		state.pos += 1
		state.col += 1
		fracPart = parseDigits(state, isDecDigit, "fractional digits")
	end

	local eb = peekByte(state)
	if eb == 0x65 or eb == 0x45 then
		isFloat = true
		state.pos += 1
		state.col += 1
		local sb = peekByte(state)
		if sb == 0x2B or sb == 0x2D then
			expSign = string.char(sb)
			state.pos += 1
			state.col += 1
		end
		expPart = parseDigits(state, isDecDigit, "exponent digits")
	end

	if isFloat then
		local piece = signStr .. intPart
		if fracPart then
			piece ..= "." .. fracPart
		end
		if expPart then
			piece ..= "e" .. expSign .. expPart
		end
		local n = tonumber(piece)
		if not n then
			throwError(state, "invalid float")
		end
		return n :: number, true
	else
		local n = tonumber(signStr .. intPart)
		if not n then
			throwError(state, "invalid integer")
		end
		return n :: number, false
	end
end

-- ===== Datetimes =====

local function looksLikeDate(state: ParserState): boolean
	if state.pos + 4 > state.len then
		return false
	end
	local s = state.src
	return isDecDigit(string.byte(s, state.pos))
		and isDecDigit(string.byte(s, state.pos + 1))
		and isDecDigit(string.byte(s, state.pos + 2))
		and isDecDigit(string.byte(s, state.pos + 3))
		and string.byte(s, state.pos + 4) == 0x2D
end

local function looksLikeTime(state: ParserState): boolean
	if state.pos + 2 > state.len then
		return false
	end
	local s = state.src
	return isDecDigit(string.byte(s, state.pos))
		and isDecDigit(string.byte(s, state.pos + 1))
		and string.byte(s, state.pos + 2) == 0x3A
end

local function parseExactDigits(state: ParserState, n: number, what: string): number
	if state.pos + n - 1 > state.len then
		throwError(state, "expected " .. what)
	end
	for i = 0, n - 1 do
		if not isDecDigit(string.byte(state.src, state.pos + i)) then
			throwError(state, "expected " .. what)
		end
	end
	local v = tonumber(string.sub(state.src, state.pos, state.pos + n - 1)) :: number
	advanceBytes(state, n)
	return v
end

local function parseTimePart(state: ParserState): (number, number, number, number)
	local h = parseExactDigits(state, 2, "hour")
	if peekByte(state) ~= 0x3A then
		throwError(state, "expected ':'")
	end
	advanceBytes(state, 1)
	local m = parseExactDigits(state, 2, "minute")
	if peekByte(state) ~= 0x3A then
		throwError(state, "expected ':'")
	end
	advanceBytes(state, 1)
	local sec = parseExactDigits(state, 2, "second")
	local ns = 0
	if peekByte(state) == 0x2E then
		advanceBytes(state, 1)
		local fracStart = state.pos
		while state.pos <= state.len and isDecDigit(string.byte(state.src, state.pos)) do
			state.pos += 1
			state.col += 1
		end
		if state.pos == fracStart then
			throwError(state, "expected fractional-second digits")
		end
		local frac = string.sub(state.src, fracStart, state.pos - 1)
		if #frac > 9 then
			frac = string.sub(frac, 1, 9)
		else
			frac = frac .. string.rep("0", 9 - #frac)
		end
		ns = tonumber(frac) :: number
	end
	if h > 23 or m > 59 or sec > 60 then
		throwError(state, "invalid time component")
	end
	return h, m, sec, ns
end

local function parseDateTime(state: ParserState): DateTimeLike
	if not looksLikeDate(state) then
		-- Must be a local time.
		local h, m, sec, ns = parseTimePart(state)
		return {
			_type = "time-local",
			hour = h,
			minute = m,
			second = sec,
			nanosecond = ns,
		} :: LocalTime
	end
	local year = parseExactDigits(state, 4, "year")
	advanceBytes(state, 1) -- '-'
	local month = parseExactDigits(state, 2, "month")
	if peekByte(state) ~= 0x2D then
		throwError(state, "expected '-'")
	end
	advanceBytes(state, 1)
	local day = parseExactDigits(state, 2, "day")
	if month < 1 or month > 12 or day < 1 or day > 31 then
		throwError(state, "invalid date component")
	end

	local sep = peekByte(state)
	local hasTime = false
	if sep == 0x54 or sep == 0x74 then -- T or t
		advanceBytes(state, 1)
		hasTime = true
	elseif sep == 0x20 then
		-- Only treat ' ' as separator when followed by HH:
		if state.pos + 3 <= state.len then
			local s = state.src
			if
				isDecDigit(string.byte(s, state.pos + 1))
				and isDecDigit(string.byte(s, state.pos + 2))
				and string.byte(s, state.pos + 3) == 0x3A
			then
				advanceBytes(state, 1)
				hasTime = true
			end
		end
	end

	if not hasTime then
		return { _type = "date-local", year = year, month = month, day = day } :: LocalDate
	end

	local h, m, sec, ns = parseTimePart(state)

	local ob = peekByte(state)
	if ob == 0x5A or ob == 0x7A then
		advanceBytes(state, 1)
		return {
			_type = "datetime",
			year = year,
			month = month,
			day = day,
			hour = h,
			minute = m,
			second = sec,
			nanosecond = ns,
			offset = "Z",
		} :: OffsetDateTime
	elseif ob == 0x2B or ob == 0x2D then
		local signCh = string.char(ob)
		advanceBytes(state, 1)
		local oh = parseExactDigits(state, 2, "offset hour")
		if peekByte(state) ~= 0x3A then
			throwError(state, "expected ':' in offset")
		end
		advanceBytes(state, 1)
		local om = parseExactDigits(state, 2, "offset minute")
		if oh > 23 or om > 59 then
			throwError(state, "invalid offset component")
		end
		return {
			_type = "datetime",
			year = year,
			month = month,
			day = day,
			hour = h,
			minute = m,
			second = sec,
			nanosecond = ns,
			offset = string.format("%s%02d:%02d", signCh, oh, om),
		} :: OffsetDateTime
	end

	return {
		_type = "datetime-local",
		year = year,
		month = month,
		day = day,
		hour = h,
		minute = m,
		second = sec,
		nanosecond = ns,
	} :: LocalDateTime
end

-- ===== Value dispatch =====

local parseValue: (ParserState) -> any
local parseArray: (ParserState) -> { any }
local parseInlineTable: (ParserState) -> { [string]: any }
local addKeyValueToTable: (ParserState, { [string]: any }, { string }, any) -> ()

-- Decides whether the next bytes look like a datetime (date or time) rather than a number.
local function isDateTimeStart(state: ParserState): boolean
	if looksLikeDate(state) then
		return true
	end
	-- A bare time (HH:MM:SS) only if it looks exactly like that pattern (no leading sign).
	return looksLikeTime(state)
end

parseValue = function(state: ParserState): any
	local b = peekByte(state)
	if b == -1 then
		throwError(state, "expected value")
	end
	if b == 0x22 then
		if peekByte(state, 1) == 0x22 and peekByte(state, 2) == 0x22 then
			return parseMultilineBasicString(state)
		end
		return parseBasicString(state)
	elseif b == 0x27 then
		if peekByte(state, 1) == 0x27 and peekByte(state, 2) == 0x27 then
			return parseMultilineLiteralString(state)
		end
		return parseLiteralString(state)
	elseif b == 0x5B then
		return parseArray(state)
	elseif b == 0x7B then
		return parseInlineTable(state)
	elseif b == 0x74 then
		if string.sub(state.src, state.pos, state.pos + 3) == "true" then
			advanceBytes(state, 4)
			return true
		end
		throwError(state, "expected 'true'")
		error("unreachable")
	elseif b == 0x66 then
		if string.sub(state.src, state.pos, state.pos + 4) == "false" then
			advanceBytes(state, 5)
			return false
		end
		throwError(state, "expected 'false'")
		error("unreachable")
	else
		if isDateTimeStart(state) then
			return parseDateTime(state)
		end
		local v, _ = parseNumberValue(state)
		return v
	end
end

parseArray = function(state: ParserState): { any }
	advanceBytes(state, 1) -- '['
	local arr: { any } = {}
	local function skipAll()
		while not eof(state) do
			local b = peekByte(state)
			if b == 0x20 or b == 0x09 or b == 0x0A then
				advanceBytes(state, 1)
			elseif b == 0x0D and peekByte(state, 1) == 0x0A then
				advanceBytes(state, 2)
			elseif b == 0x23 then
				skipComment(state)
			else
				break
			end
		end
	end
	skipAll()
	if peekByte(state) == 0x5D then
		advanceBytes(state, 1)
		return arr
	end
	while true do
		skipAll()
		table.insert(arr, parseValue(state))
		skipAll()
		local b = peekByte(state)
		if b == 0x2C then
			advanceBytes(state, 1)
			skipAll()
			if peekByte(state) == 0x5D then
				advanceBytes(state, 1)
				return arr
			end
		elseif b == 0x5D then
			advanceBytes(state, 1)
			return arr
		else
			throwError(state, "expected ',' or ']' in array")
		end
	end
end

addKeyValueToTable = function(state: ParserState, current: { [string]: any }, keys: { string }, value: any)
	for i = 1, #keys - 1 do
		local k = keys[i]
		local existing = current[k]
		if existing == nil then
			local newTbl = {}
			state.dotted[newTbl] = true
			state.dottedOpen[newTbl] = true
			current[k] = newTbl
			current = newTbl
		elseif typeof(existing) == "table" then
			if state.arrays[existing] then
				throwError(state, string.format("cannot extend array of tables '%s' with dotted key", k))
			elseif state.inline[existing] then
				throwError(state, string.format("cannot extend inline table '%s'", k))
			elseif state.dottedOpen[existing] then
				current = existing
			else
				-- Any explicit [table], or a closed dotted table from a previous scope.
				throwError(state, string.format("cannot extend already-defined table '%s' via dotted key", k))
			end
		else
			throwError(state, string.format("cannot extend non-table key '%s'", k))
		end
	end
	local leaf = keys[#keys]
	if current[leaf] ~= nil then
		throwError(state, string.format("duplicate key '%s'", leaf))
	end
	current[leaf] = value
end

parseInlineTable = function(state: ParserState): { [string]: any }
	advanceBytes(state, 1) -- '{'
	local tbl: { [string]: any } = {}
	state.inline[tbl] = true
	skipWhitespace(state)
	if peekByte(state) == 0x7D then
		advanceBytes(state, 1)
		return tbl
	end
	-- Dotted keys inside an inline table should use the inline table's own scope for dotted tracking.
	-- We temporarily snapshot and restore the outer dottedOpen set.
	local savedDottedOpen = state.dottedOpen
	state.dottedOpen = {}
	while true do
		skipWhitespace(state)
		local keys = parseKey(state)
		skipWhitespace(state)
		if peekByte(state) ~= 0x3D then
			state.dottedOpen = savedDottedOpen
			throwError(state, "expected '=' in inline table")
		end
		advanceBytes(state, 1)
		skipWhitespace(state)
		local v = parseValue(state)
		addKeyValueToTable(state, tbl, keys, v)
		skipWhitespace(state)
		local b = peekByte(state)
		if b == 0x2C then
			advanceBytes(state, 1)
			skipWhitespace(state)
			if peekByte(state) == 0x7D then
				state.dottedOpen = savedDottedOpen
				throwError(state, "trailing comma not allowed in inline table")
			end
		elseif b == 0x7D then
			advanceBytes(state, 1)
			state.dottedOpen = savedDottedOpen
			return tbl
		elseif b == 0x0A or (b == 0x0D and peekByte(state, 1) == 0x0A) then
			state.dottedOpen = savedDottedOpen
			throwError(state, "newline not allowed in inline table")
		else
			state.dottedOpen = savedDottedOpen
			throwError(state, "expected ',' or '}' in inline table")
		end
	end
end

-- ===== Table headers =====

-- Navigate through the parent segments of a path, creating intermediates as needed.
-- For arrays-of-tables encountered in the path, navigate into the last element.
local function navigateTableHeaderPath(state: ParserState, keys: { string }): { [string]: any }
	local current: { [string]: any } = state.root
	for i = 1, #keys - 1 do
		local k = keys[i]
		local existing = current[k]
		if existing == nil then
			local newTbl = {}
			current[k] = newTbl
			current = newTbl
		elseif typeof(existing) == "table" then
			if state.arrays[existing] then
				local arr = existing :: { any }
				current = arr[#arr]
			elseif state.inline[existing] then
				throwError(state, string.format("cannot extend inline table '%s'", k))
			else
				current = existing
			end
		else
			throwError(state, string.format("cannot extend non-table '%s'", k))
		end
	end
	return current
end

local function parseTableHeader(state: ParserState): { [string]: any }
	advanceBytes(state, 1) -- '['
	skipWhitespace(state)
	local keys = parseKey(state)
	skipWhitespace(state)
	if peekByte(state) ~= 0x5D then
		throwError(state, "expected ']'")
	end
	advanceBytes(state, 1)

	local parent = navigateTableHeaderPath(state, keys)
	local leaf = keys[#keys]
	local existing = parent[leaf]

	local target: { [string]: any }
	if existing == nil then
		target = {}
		state.explicit[target] = true
		parent[leaf] = target
	elseif typeof(existing) == "table" then
		if state.arrays[existing] then
			throwError(state, string.format("cannot redefine array of tables '%s' as table", leaf))
		elseif state.inline[existing] then
			throwError(state, string.format("cannot extend inline table '%s'", leaf))
		elseif state.dotted[existing] then
			throwError(state, string.format("cannot redefine table '%s' (created by dotted keys)", leaf))
		elseif state.explicit[existing] then
			throwError(state, string.format("duplicate table definition '%s'", leaf))
		else
			-- Implicit: upgrade to explicit.
			state.explicit[existing] = true
			target = existing
		end
		target = existing
	else
		throwError(state, string.format("cannot redefine non-table '%s' as table", leaf))
	end
	return target
end

local function parseArrayTableHeader(state: ParserState): { [string]: any }
	advanceBytes(state, 2) -- '[['
	skipWhitespace(state)
	local keys = parseKey(state)
	skipWhitespace(state)
	if peekByte(state) ~= 0x5D or peekByte(state, 1) ~= 0x5D then
		throwError(state, "expected ']]'")
	end
	advanceBytes(state, 2)

	local parent = navigateTableHeaderPath(state, keys)
	local leaf = keys[#keys]
	local existing = parent[leaf]
	local arr: { any }

	if existing == nil then
		arr = {}
		state.arrays[arr] = true
		parent[leaf] = arr
	elseif typeof(existing) == "table" and state.arrays[existing] then
		arr = existing :: { any }
	elseif typeof(existing) == "table" then
		throwError(state, string.format("cannot redefine table '%s' as array of tables", leaf))
	else
		throwError(state, string.format("cannot redefine non-table '%s' as array of tables", leaf))
	end

	local entry: { [string]: any } = {}
	state.explicit[entry] = true
	table.insert(arr, entry)
	return entry
end

-- ===== Top-level loop =====

local function deserialize(input: string): { [string]: any }
	local state: ParserState = {
		src = input,
		len = #input,
		pos = 1,
		line = 1,
		col = 1,
		root = {},
		explicit = {},
		inline = {},
		dotted = {},
		arrays = {},
		dottedOpen = {},
	}

	-- Strip UTF-8 BOM if present.
	if state.len >= 3 and string.sub(input, 1, 3) == "\xEF\xBB\xBF" then
		state.pos = 4
	end

	local currentTable: { [string]: any } = state.root

	while not eof(state) do
		skipWhitespace(state)
		local b = peekByte(state)
		if b == -1 then
			break
		elseif b == 0x0A then
			advanceBytes(state, 1)
		elseif b == 0x0D then
			if peekByte(state, 1) == 0x0A then
				advanceBytes(state, 2)
			else
				throwError(state, "bare carriage return not allowed")
			end
		elseif b == 0x23 then
			skipComment(state)
		elseif b == 0x5B then
			if peekByte(state, 1) == 0x5B then
				currentTable = parseArrayTableHeader(state)
			else
				currentTable = parseTableHeader(state)
			end
			-- Entering a new scope resets the set of dotted tables that can still be extended.
			state.dottedOpen = {}
			requireLineEnd(state)
		else
			local keys = parseKey(state)
			skipWhitespace(state)
			if peekByte(state) ~= 0x3D then
				throwError(state, "expected '=' after key")
			end
			advanceBytes(state, 1)
			skipWhitespace(state)
			local value = parseValue(state)
			addKeyValueToTable(state, currentTable, keys, value)
			requireLineEnd(state)
		end
	end

	return state.root
end

-- ===== Serializer =====

local BARE_KEY_PATTERN = "^[A-Za-z0-9_%-]+$"

local function isBareKey(key: string): boolean
	if key == "" then
		return false
	end
	return string.match(key, BARE_KEY_PATTERN) ~= nil
end

local function escapeBasicString(s: string): string
	local out: { string } = { '"' }
	local i = 1
	local len = #s
	while i <= len do
		local b = string.byte(s, i)
		if b == 0x22 then
			table.insert(out, '\\"')
		elseif b == 0x5C then
			table.insert(out, "\\\\")
		elseif b == 0x08 then
			table.insert(out, "\\b")
		elseif b == 0x09 then
			table.insert(out, "\\t")
		elseif b == 0x0A then
			table.insert(out, "\\n")
		elseif b == 0x0C then
			table.insert(out, "\\f")
		elseif b == 0x0D then
			table.insert(out, "\\r")
		elseif b < 0x20 or b == 0x7F then
			table.insert(out, string.format("\\u%04X", b))
		else
			table.insert(out, string.sub(s, i, i))
		end
		i += 1
	end
	table.insert(out, '"')
	return table.concat(out)
end

local function formatKey(key: string): string
	if isBareKey(key) then
		return key
	end
	return escapeBasicString(key)
end

local function formatKeyPath(parts: { string }): string
	local segs: { string } = {}
	for i, p in parts do
		segs[i] = formatKey(p)
	end
	return table.concat(segs, ".")
end

local function formatNumber(n: number): string
	if n ~= n then
		return "nan"
	elseif n == math.huge then
		return "inf"
	elseif n == -math.huge then
		return "-inf"
	elseif n == math.floor(n) and n >= -9007199254740992 and n <= 9007199254740992 then
		return string.format("%d", n)
	else
		local s = tostring(n)
		if not string.find(s, "[.eE]") then
			s ..= ".0"
		end
		return s
	end
end

local function trimTrailingZeros(s: string): string
	local result = string.gsub(s, "0+$", "")
	return result
end

local function formatFraction(ns: number): string
	if ns <= 0 then
		return ""
	end
	local s = string.format("%09d", ns)
	s = trimTrailingZeros(s)
	if s == "" then
		return ""
	end
	return "." .. s
end

local function isDateTimeLike(v: any): boolean
	if typeof(v) ~= "table" then
		return false
	end
	local t = v._type
	return t == "datetime" or t == "datetime-local" or t == "date-local" or t == "time-local"
end

local function formatDateTime(v: any): string
	local t = v._type
	if t == "date-local" then
		return string.format("%04d-%02d-%02d", v.year, v.month, v.day)
	elseif t == "time-local" then
		return string.format("%02d:%02d:%02d", v.hour, v.minute, v.second) .. formatFraction(v.nanosecond or 0)
	elseif t == "datetime-local" then
		return string.format(
			"%04d-%02d-%02dT%02d:%02d:%02d",
			v.year,
			v.month,
			v.day,
			v.hour,
			v.minute,
			v.second
		) .. formatFraction(v.nanosecond or 0)
	else
		return string.format(
			"%04d-%02d-%02dT%02d:%02d:%02d",
			v.year,
			v.month,
			v.day,
			v.hour,
			v.minute,
			v.second
		) .. formatFraction(v.nanosecond or 0) .. tostring(v.offset)
	end
end

local isPureArray: (any) -> boolean
isPureArray = function(v: any): boolean
	if typeof(v) ~= "table" then
		return false
	end
	if isDateTimeLike(v) then
		return false
	end
	local n = 0
	for _ in v :: { [any]: any } do
		n += 1
	end
	if n == 0 then
		return false
	end
	for i = 1, n do
		if (v :: { [number]: any })[i] == nil then
			return false
		end
	end
	return true
end

local function isPlainTable(v: any): boolean
	return typeof(v) == "table" and not isDateTimeLike(v) and not isPureArray(v)
end

local function isArrayOfTables(arr: { any }): boolean
	if #arr == 0 then
		return false
	end
	for _, v in arr do
		if not isPlainTable(v) then
			return false
		end
	end
	return true
end

local serializeInlineValue: (any) -> string

local function serializeInlineArray(arr: { any }): string
	if #arr == 0 then
		return "[]"
	end
	local parts: { string } = { "[" }
	for i, v in arr do
		if i > 1 then
			table.insert(parts, ", ")
		end
		table.insert(parts, serializeInlineValue(v))
	end
	table.insert(parts, "]")
	return table.concat(parts)
end

local function serializeInlineTable(tbl: { [string]: any }): string
	local entries: { string } = {}
	for k, v in tbl do
		table.insert(entries, formatKey(tostring(k)) .. " = " .. serializeInlineValue(v))
	end
	if #entries == 0 then
		return "{}"
	end
	return "{ " .. table.concat(entries, ", ") .. " }"
end

serializeInlineValue = function(v: any): string
	local tp = typeof(v)
	if tp == "string" then
		return escapeBasicString(v :: string)
	elseif tp == "number" then
		return formatNumber(v :: number)
	elseif tp == "boolean" then
		return (v :: boolean) and "true" or "false"
	elseif tp == "table" then
		if isDateTimeLike(v) then
			return formatDateTime(v)
		elseif isPureArray(v) then
			return serializeInlineArray(v :: { any })
		else
			return serializeInlineTable(v :: { [string]: any })
		end
	end
	error("unsupported TOML value type: " .. tp)
end

type SerializeContext = {
	out: { string },
	firstSection: boolean,
}

local writeTableSection: (SerializeContext, { [string]: any }, { string }) -> ()

local function writeKeyValueLine(ctx: SerializeContext, key: string, value: any)
	table.insert(ctx.out, formatKey(key) .. " = " .. serializeInlineValue(value) .. "\n")
end

local function writeHeader(ctx: SerializeContext, path: { string }, double: boolean)
	if not ctx.firstSection then
		table.insert(ctx.out, "\n")
	end
	ctx.firstSection = false
	if double then
		table.insert(ctx.out, "[[" .. formatKeyPath(path) .. "]]\n")
	else
		table.insert(ctx.out, "[" .. formatKeyPath(path) .. "]\n")
	end
end

-- Returns true if the table has any direct key-value pairs (not sub-tables / arrays of tables).
local function hasDirectValues(tbl: { [string]: any }): boolean
	for _, v in tbl do
		if isPlainTable(v) then
			continue
		end
		if isPureArray(v) and isArrayOfTables(v :: { any }) then
			continue
		end
		return true
	end
	return false
end

local function isEmptyTable(tbl: { [string]: any }): boolean
	return next(tbl) == nil
end

writeTableSection = function(ctx: SerializeContext, tbl: { [string]: any }, path: { string })
	local directKeys: { string } = {}
	local subTableKeys: { string } = {}
	local arrayTableKeys: { string } = {}

	for k, v in tbl do
		local kStr = tostring(k)
		if isPlainTable(v) then
			table.insert(subTableKeys, kStr)
		elseif isPureArray(v) and isArrayOfTables(v :: { any }) then
			table.insert(arrayTableKeys, kStr)
		else
			table.insert(directKeys, kStr)
		end
	end

	table.sort(directKeys)
	table.sort(subTableKeys)
	table.sort(arrayTableKeys)

	for _, k in directKeys do
		writeKeyValueLine(ctx, k, tbl[k])
	end

	for _, k in subTableKeys do
		local sub = tbl[k] :: { [string]: any }
		local subPath = table.clone(path) :: { string }
		table.insert(subPath, k)
		-- Only emit a `[path]` header when the sub-table has direct values or is
		-- empty; otherwise its own sub-tables/array-tables will produce headers.
		if hasDirectValues(sub) or isEmptyTable(sub) then
			writeHeader(ctx, subPath, false)
		end
		writeTableSection(ctx, sub, subPath)
	end

	for _, k in arrayTableKeys do
		local arr = tbl[k] :: { { [string]: any } }
		local subPath = table.clone(path) :: { string }
		table.insert(subPath, k)
		for _, entry in arr do
			writeHeader(ctx, subPath, true)
			writeTableSection(ctx, entry, subPath)
		end
	end
end

local function serialize(tbl: { [string]: any }): string
	local ctx: SerializeContext = { out = {}, firstSection = true }
	writeTableSection(ctx, tbl, {})
	return table.concat(ctx.out)
end

toml.serialize = serialize
toml.deserialize = deserialize

return table.freeze(toml)
