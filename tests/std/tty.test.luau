--!strict
local fs = require("@std/fs")
local path = require("@std/path")
local process = require("@std/process")
local system = require("@std/system")
local test = require("@std/test")

local lutePath = path.format(process.execPath())
local tmpDir = system.tmpdir()

local function runScript(name: string, script: string): process.ProcessResult
	local file = path.join(tmpDir, `{name}.luau`)
	fs.writeStringToFile(file, script)
	local result = process.run({ lutePath, path.format(file) })
	fs.remove(file)
	return result
end

test.suite("TTYSuite", function(suite)
	suite:case("ttyApiLoads", function(assert)
		local r = runScript(
			"tty_api_loads",
			[[
			local tty = require("@std/tty")
			print(type(tty.inputIsTTY))
			print(type(tty.outputIsTTY))
			print(type(tty.setRawMode))
			print(type(tty.read))
			print(type(tty.withRawMode))
		]]
		)

		assert.eq(r.exitcode, 0)
		assert.strContains(r.stdout, "function")
	end)

	suite:case("ttyReportsNonTTYUnderCapturedProcess", function(assert)
		local r = runScript(
			"tty_reports_notty",
			[[
			local tty = require("@std/tty")
			print(tty.inputIsTTY())
			print(tty.outputIsTTY())
			print(tty.isRawMode())
		]]
		)

		assert.eq(r.exitcode, 0)
		assert.eq(r.stdout, "false\nfalse\nfalse\n")
	end)

	suite:case("ttyRawModeErrorsWhenInputIsNotTTY", function(assert)
		local r = runScript(
			"tty_rawmode_error",
			[[
			local tty = require("@std/tty")
			tty.setRawMode(true)
		]]
		)

		assert.eq(r.exitcode, 1)
		assert.strContains(r.stderr, "stdin is not attached to a TTY")
	end)

	suite:case("ttyWindowSizeErrorsWhenOutputIsNotTTY", function(assert)
		local r = runScript(
			"tty_winsize_error",
			[[
			local tty = require("@std/tty")
			tty.getWindowSize()
		]]
		)

		assert.eq(r.exitcode, 1)
		assert.strContains(r.stderr, "stdout is not attached to a TTY")
	end)
end)
