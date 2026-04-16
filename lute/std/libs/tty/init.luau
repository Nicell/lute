--!strict
-- @std/tty
-- stdlib for `@lute/tty`
-- Provides terminal-specific capabilities such as raw mode and window size

local tty = require("@lute/tty")

local ttylib = {}

ttylib.inputIsTTY = tty.inputIsTTY
ttylib.outputIsTTY = tty.outputIsTTY
ttylib.isRawMode = tty.isRawMode
ttylib.setRawMode = tty.setRawMode
ttylib.read = tty.read
ttylib.getWindowSize = tty.getWindowSize

function ttylib.withRawMode<T...>(callback: (T...) -> ...unknown, ...: T...): ...unknown
	local wasRaw = tty.isRawMode()
	if not wasRaw then
		tty.setRawMode(true)
	end

	local result = table.pack(pcall(function(...: T...): ...unknown
		return callback(...)
	end, ...))

	if not wasRaw then
		tty.setRawMode(false)
	end

	if not result[1] then
		error(result[2], 0)
	end

	return table.unpack(result, 2, result.n)
end

return table.freeze(ttylib)
