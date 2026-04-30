local fs = require("@std/fs")
local pathlib = require("@std/path")
local process = require("@std/process")
local system = require("@std/system")
local test = require("@std/test")

local tmpdir = system.tmpdir()

local rundir = pathlib.join(tmpdir, "lute-run")

local lutePath = pathlib.format(process.execPath())

test.suite("LuteRun", function(suite)
	suite:beforeEach(function()
		if fs.exists(rundir) then
			fs.removeDirectory(rundir, { recursive = true })
		end
	end)

	suite:case("sameNamedFileAndDirectory", function(assert)
		-- Setup
		fs.createDirectory(rundir)
		fs.createDirectory(pathlib.join(rundir, "hello"))

		local helloFilePath = pathlib.join(rundir, "hello.luau")
		fs.writeStringToFile(helloFilePath, "print('Hello world')")

		-- Execute
		local result = process.run({ lutePath, pathlib.format(helloFilePath) })

		assert.eq(result.exitcode, 1)
		assert.neq(
			result.stderr:find(
				`Error while resolving filepath '.+{if system.win32 then "\\" else "/"}hello%.luau': Unable to tell whether path is a file or directory%. Is there a same%-named file or directory%?`
			),
			nil
		)
		assert.strNotContains(result.stdout, "Hello world")
	end)

	suite:case("scriptReceivesResolvedPathAsFirstArgument", function(assert)
		fs.createDirectory(rundir)
		fs.createDirectory(pathlib.join(rundir, ".lute"))

		local scriptPath = pathlib.join(rundir, ".lute", "example.luau")
		fs.writeStringToFile(
			scriptPath,
			[[
			local io = require("@std/io")
			local process = require("@std/process")
			local args = { ... }

			io.write(args[1] .. "\n")
			io.write(args[2] .. "\n")
			io.write(process.args[1] .. "\n")
			io.write(process.args[2])
		]]
		)

		local explicitScriptPath = pathlib.format(pathlib.join(".lute", "example.luau"))
		local resolvedScriptPaths = {}
		local expectedSuffix = pathlib.format(pathlib.join(".lute", "example.luau"))

		for _, commandScriptPath in { "example", explicitScriptPath } do
			local result = process.run(
				{ lutePath, "run", commandScriptPath, "extra" },
				{ cwd = pathlib.format(rundir) }
			)
			local output = result.stdout:split("\n")

			assert.eq(result.exitcode, 0)
			assert.eq(output[1], output[3])
			assert.eq(output[2], "extra")
			assert.eq(output[4], "extra")
			assert.eq(output[1]:sub(-#expectedSuffix), expectedSuffix)

			table.insert(resolvedScriptPaths, output[1])
		end

		assert.eq(resolvedScriptPaths[1], resolvedScriptPaths[2])
	end)
end)
