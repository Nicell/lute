-- Worker module loaded into a child VM by `vm.create`. The parent pulls files
-- from a shared cursor and dispatches each to an idle worker via `runfile`,
-- giving work-stealing load balancing across workers. Only primitive values
-- are returned so the result can be marshalled across the VM boundary.
local luau = require("@std/luau")
local env = require("@std/test/env")
local runner = require("@std/test/runner")
local types = require("@std/test/types")
local filter = require("./filter")

local function reset()
	table.clear(env.anonymous)
	table.clear(env.suites)
	table.clear(env.suiteindex)
	table.clear(env.caseindex)
end

local function loadFailure(filepath: string, err: any): types.FailedTest
	return {
		test = filepath,
		failure = {
			msg = `failed to load test file: {tostring(err)}`,
			stacktrace = "",
			__tag = "runtime_error",
		},
	}
end

local M = {}

function M.runfile(args: { file: string, suite: string?, case: string? }): types.TestRunResult
	reset()

	local ok, err = pcall(luau.loadModule, args.file, nil)
	if not ok then
		return {
			failures = { loadFailure(args.file, err) },
			total = 1,
			passed = 0,
			failed = 1,
		}
	end

	local filtered = filter.filtertests(env, args.suite, args.case)
	return runner.run(filtered)
end

return M
