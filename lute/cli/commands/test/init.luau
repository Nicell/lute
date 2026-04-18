local cli = require("@batteries/cli")
local finder = require("@self/finder")
local luau = require("@std/luau")
local path = require("@std/path")
local ps = require("@std/process")
local system = require("@std/system")
local task = require("@std/task")
local vm = require("@lute/vm")
local testtypes = require("@std/test/types")
local reporter = require("@self/reporter")
local test = require("@std/test")
local snaprunner = require("@self/snap/runner")
local updater = require("@self/snap/updater")

local function loadtests(testfiles: { path.Path })
	for _, p in testfiles do
		local success, err = pcall(luau.loadModule, p, nil)

		if not success then
			print(`Error loading {path.format(p)}: {err}`)
			ps.exit(1)
		end
	end
end

local function listtests()
	local registered = test._registered()

	print("Anonymous:")
	for _, tc in registered.anonymous do
		print(`\t{tc.name}`)
	end

	for _, suite in registered.suites do
		print(`{suite.name}:`)
		for _, tc in suite.cases do
			print(`\t{tc.name}`)
		end
	end
end

-- Spawns up to `system.threadCount()` persistent child VM workers and has each
-- pull the next file from a shared cursor as it becomes free. This naturally
-- balances load even when a few files dominate the total runtime. Each worker
-- keeps a fresh `@std/test/env` (reset per file) so registered test state is
-- never shared across concurrent runs. Cross-VM calls are cheap — packing is
-- a few scalars and a small result table — so one call per file is fine.
local function runtests(testfiles: { path.Path }, suiteName: string?, caseName: string?)
	local result: testtypes.TestRunResult = { failures = {}, total = 0, failed = 0, passed = 0 }
	local nfiles = #testfiles

	if nfiles > 0 then
		local nworkers = math.max(1, math.min(system.threadCount(), nfiles))

		local cursor = 1
		-- Luau coroutines are cooperative and only yield at explicit points,
		-- so this increment is safe without a lock: the cross-VM call yields,
		-- not the cursor read itself.
		local function takenext(): path.Path?
			if cursor > nfiles then
				return nil
			end
			local p = testfiles[cursor]
			cursor += 1
			return p
		end

		local done = 0
		for _ = 1, nworkers do
			task.spawn(function()
				local worker = vm.create("@self/worker")
				while true do
					local p = takenext()
					if not p then
						break
					end
					local r = worker.runfile({
						file = path.format(p),
						suite = suiteName,
						case = caseName,
					})
					result.total += r.total
					result.passed += r.passed
					result.failed += r.failed
					for _, f in r.failures do
						table.insert(result.failures, f)
					end
				end
				done += 1
			end)
		end

		while done < nworkers do
			task.wait()
		end
	end

	reporter.color(result)
	if result.failed ~= 0 then
		ps.exit(1)
	end
	ps.exit(0)
end

local function runsnapshots(searchpaths: { string }, update: boolean, interactive: boolean)
	local files = finder.findsnapfiles(searchpaths)

	if #files == 0 then
		print("No snapshot files (*.snap.luau) found.")
		ps.exit(0)
		return
	end

	local result = snaprunner.runsnapshots(files)
	reporter.snap(result)
	updater.updatesnapshot(result, update, interactive)
end

local function main(...: string)
	local args = cli.parser()

	args:add("help", "flag", { help = "Show this help message", aliases = { "h" } })
	args:add("list", "flag", { help = "List all discovered test cases without running them" })
	args:add("suite", "option", { help = "Run only tests in the specified suite", aliases = { "s" } })
	args:add("case", "option", { help = "Run only test cases matching the specified name", aliases = { "c" } })
	args:add("interactive", "flag", { help = "Interactively accept/reject snapshot changes", aliases = { "i" } })
	args:add("update", "flag", { help = "Auto-accept all snapshot changes", aliases = { "u" } })

	args:parse({ ... })

	if args:has("help") then
		print(args:help())
		return
	end

	local forwarded = args:forwarded()
	-- Check if this is a snapshot subcommand
	if forwarded and #forwarded > 0 and forwarded[1] == "snapshots" then
		if #forwarded == 1 then
			runsnapshots({ "./tests" }, args:has("update"), args:has("interactive"))
		else
			local searchpaths = {}
			for idx = 2, #forwarded do
				table.insert(searchpaths, forwarded[idx])
			end
			runsnapshots(searchpaths, args:has("update"), args:has("interactive"))
		end
	end

	local searchpath = if forwarded and #forwarded > 0 then forwarded else { "./" }
	local testfiles = finder.findtestfiles(searchpath)

	if args:has("list") then
		loadtests(testfiles)
		listtests()
	else
		runtests(testfiles, args:get("suite"), args:get("case"))
	end
end

main(...)
